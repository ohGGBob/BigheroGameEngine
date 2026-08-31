#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inVertColor;
layout(location = 4) in vec3 inTangent;

layout(location = 0) out vec4 outColor;

// set1 binding0：光照/环境/阴影参数；binding1：反照率；binding2：法线贴图；binding3：方向光阴影贴图
struct PointLight
{
    vec3 position;
    float intensity;
    vec3 color;
    float radius;
    float castsShadow; // 1.0 表示启用立方体阴影
    float pad[3];      // std140 数组步长须为 16 的倍数：结构体凑到 48 字节
};

layout(set = 1, binding = 0, std140) uniform LightUBO {
    vec3 lightDir;
    float dirIntensity;
    vec3 lightColor;
    float ambientFactor;
    vec3 cameraPos;
    float pointLightCount;
    float shadowStrength;
    float shadowBias;
    float iblStrength;
    float exposure;
    mat4 lightSpaceMatrix;
    PointLight lights[8];
} lightUbo;

layout(set = 1, binding = 1) uniform sampler2D albedoTex;
layout(set = 1, binding = 2) uniform sampler2D normalTex;
layout(set = 1, binding = 3) uniform sampler2D shadowMap;
layout(set = 1, binding = 4) uniform samplerCube envMap;
layout(set = 1, binding = 5) uniform samplerCube irradianceMap;
layout(set = 1, binding = 6) uniform samplerCube prefilteredMap;
layout(set = 1, binding = 7) uniform sampler2D brdfLut;
layout(set = 1, binding = 8) uniform samplerCube pointShadowMap;

// 推送常量：与顶点阶段同一块，这里读取材质参数
layout(push_constant) uniform PushObject {
    mat4 model;
    vec4 tint;
    float metallic;
    float roughness;
} pushObject;

const float PI = 3.14159265358979;

// NDF：GGX/Trowbridge-Reitz
float distributionGGX(vec3 N, vec3 H, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float NdotH = max(dot(N, H), 0.0);
    const float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

// 几何项：Schlick-GGX（直接光照k=(r+1)^2/8）
float geometrySchlickGGX(float NdotV, float roughness)
{
    const float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    const float NdotV = max(dot(N, V), 0.0);
    const float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// 菲涅尔：Schlick近似
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 菲涅尔（含粗糙度）：IBL环境光用
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    const vec3 Fmax = max(vec3(1.0 - roughness), F0);
    return F0 + (Fmax - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Cook-Torrance BRDF（不含辐射率与NdotL）
vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness)
{
    const vec3 H = normalize(V + L);
    const vec3 F0 = mix(vec3(0.04), albedo, metallic);
    const float NdotV = max(dot(N, V), 0.0);
    const float NdotL = max(dot(N, L), 0.0);

    const float D = distributionGGX(N, H, roughness);
    const float G = geometrySmith(N, V, L, roughness);
    const vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    const vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-7);
    const vec3 kd = (1.0 - F) * (1.0 - metallic);
    return kd * albedo / PI + specular;
}

// 方向光阴影：3x3 PCF软采样 + 偏移防阴影痤疮
float shadowFactor(vec4 fragPosLightSpace, float NdotL)
{
    vec3 proj = fragPosLightSpace.xyz / fragPosLightSpace.w;
    proj = proj * 0.5 + 0.5;
    // 光照视锥体之外不受阴影
    if (proj.z >= 1.0 ||
        proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    // 斜率比例偏移：掠射角加大偏移
    const float bias = max(lightUbo.shadowBias * (1.0 - NdotL), lightUbo.shadowBias * 0.1);

    const vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            const float sampled = texture(shadowMap, proj.xy + vec2(x, y) * texelSize).r;
            shadow += (proj.z - bias > sampled) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

// 点光源立方体阴影：PCF 软采样 + 手调深度偏移
// fragToLight 为从片元指向点光源的单位方向；fragDepth 为片元到光源的深度（需除以 lightRadius 归一化）
float pointShadowFactor(vec3 fragToLight, float fragDepth, float lightRadius)
{
    if (fragToLight == vec3(0.0))
        return 1.0;

    const float bias = lightUbo.shadowBias * 0.4; // 立方体阴影独立调偏（前端剔除已缓解痤疮）
    const float normalizedDepth = fragDepth / lightRadius;

    // 3x3x3 立方体邻域 PCF，跨面平滑过渡
    const float texelSize = 1.0 / float(textureSize(pointShadowMap, 0).x);
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int z = -1; z <= 1; ++z)
            {
                const vec3 offset = vec3(x, y, z) * texelSize;
                const float closest = texture(pointShadowMap, fragToLight + offset).r;
                shadow += (normalizedDepth - bias > closest) ? 0.0 : 1.0;
            }
        }
    }
    return shadow / 27.0;
}

// ACES近似色调映射（Narkowicz），压缩高光防过曝
vec3 acesFilm(vec3 x)
{
    x *= 0.8;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    // ---- TBN构建与法线贴图 ----
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent - N * dot(N, inTangent)); // Gram-Schmidt正交化
    vec3 B = cross(N, T);
    const mat3 TBN = mat3(T, B, N);

    vec3 mapped = texture(normalTex, inUV).xyz * 2.0 - 1.0;
    N = normalize(TBN * mapped);
    // 防止法线背向视线导致的负值光照
    const vec3 V = normalize(lightUbo.cameraPos - inWorldPos);
    N = faceforward(N, -V, N);

    // ---- 材质参数 ----
    const vec3 albedo = inVertColor * texture(albedoTex, inUV).rgb;
    const float metallic = clamp(pushObject.metallic, 0.0, 1.0);
    const float roughness = clamp(pushObject.roughness, 0.045, 1.0);

    vec3 lo = vec3(0.0);

    // ---- 方向光 + PCF阴影 ----
    {
        const vec3 L = normalize(-lightUbo.lightDir);
        const float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            const vec3 radiance = lightUbo.lightColor * lightUbo.dirIntensity;
            vec3 contrib = brdf(N, V, L, albedo, metallic, roughness) * radiance * NdotL;

            if (lightUbo.shadowStrength > 0.0)
            {
                const float shadow = shadowFactor(lightUbo.lightSpaceMatrix * vec4(inWorldPos, 1.0), NdotL);
                contrib *= mix(1.0, shadow, lightUbo.shadowStrength);
            }
            lo += contrib;
        }
    }

    // ---- 点光源循环（平方衰减+半径窗口） ----
    for (int i = 0; i < 8; ++i)
    {
        if (float(i) >= lightUbo.pointLightCount)
            break;

        const vec3 toLight = lightUbo.lights[i].position - inWorldPos;
        const float dist = length(toLight);
        if (dist >= lightUbo.lights[i].radius)
            continue;

        const vec3 L = toLight / max(dist, 1e-4);
        const float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0)
            continue;

        float attenuation = 1.0 / max(dist * dist, 0.01);
        float window = clamp(1.0 - dist / lightUbo.lights[i].radius, 0.0, 1.0);
        attenuation *= window * window;

        const vec3 radiance = lightUbo.lights[i].color * lightUbo.lights[i].intensity * attenuation;
        vec3 contrib = brdf(N, V, L, albedo, metallic, roughness) * radiance * NdotL;

        // 点光源立方体阴影（启用且浓度>0 时采样）
        if (lightUbo.lights[i].castsShadow > 0.5 && lightUbo.shadowStrength > 0.0)
        {
            // 从点光源指向片元的方向 = -(fragToLight)，查询对应面深度
            const vec3 lightToFrag = normalize(-toLight);
            const float shadow = pointShadowFactor(-lightToFrag, dist, lightUbo.lights[i].radius);
            contrib *= mix(1.0, shadow, lightUbo.shadowStrength);
        }
        lo += contrib;
    }

    // ---- 环境光：IBL（分裂求和）与常数环境光按IBL强度混合 ----
    const vec3 F0 = mix(vec3(0.04), albedo, metallic);
    const float NdotV = max(dot(N, V), 0.0);
    const vec3 F = fresnelSchlickRoughness(NdotV, F0, roughness);
    const vec3 kd = (1.0 - F) * (1.0 - metallic);

    const vec3 irradiance = texture(irradianceMap, N).rgb;
    const vec3 diffuse = irradiance * albedo;

    const vec3 R = reflect(-V, N);
    const vec3 prefiltered = textureLod(prefilteredMap, R, roughness * 4.0).rgb;
    const vec2 envBrdf = texture(brdfLut, vec2(NdotV, roughness)).rg;
    const vec3 specular = prefiltered * (F0 * envBrdf.x + envBrdf.y);

    const vec3 iblAmbient = (kd * diffuse + specular) * lightUbo.ambientFactor;

    const vec3 ambientTint = mix(vec3(1.0), F0, metallic);
    const vec3 constAmbient = lightUbo.ambientFactor * albedo * ambientTint;

    const vec3 ambient = mix(constAmbient, iblAmbient, clamp(lightUbo.iblStrength, 0.0, 1.0));
    const vec3 color = lo + ambient;

    // ACES色调映射后输出线性颜色（sRGB交换链负责编码）
    outColor = vec4(acesFilm(color * lightUbo.exposure), 1.0);
}
