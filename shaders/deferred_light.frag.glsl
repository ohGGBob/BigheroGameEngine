#version 450
#extension GL_ARB_separate_shader_objects : enable

// 延迟光照通道片段着色器：从 GBuffer 输入附件读取几何信息，
// 复用与 forward 一致的 PBR/阴影/IBL 光照模型，输出最终颜色到交换链。
// 背景像素（无几何，gPosition.a==0）直接采样环境立方图作为天空。

// GBuffer 输入附件（几何子通道写入）：set2 binding 0/1/2，input_attachment_index 对应附件下标
layout(input_attachment_index = 0, set = 2, binding = 0) uniform subpassInput gAlbedo;   // rgb=反照率, a=金属度
layout(input_attachment_index = 1, set = 2, binding = 1) uniform subpassInput gNormal;   // rgb=世界法线, a=粗糙度
layout(input_attachment_index = 2, set = 2, binding = 2) uniform subpassInput gPosition; // rgb=世界坐标, a=几何标记

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// 推送常量：逆视图投影矩阵，用于背景像素重建天空方向
layout(push_constant) uniform PushConstants
{
    mat4 invViewProj; // offset 0, 64 字节
} pc;

// 与 forward frag.glsl 完全一致的光照 UBO 与采样器布局（set1）
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

// 几何项：Schlick-GGX
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

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    const vec3 Fmax = max(vec3(1.0 - roughness), F0);
    return F0 + (Fmax - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Cook-Torrance BRDF（不含辐射率与 NdotL）
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

// 方向光阴影：3x3 PCF
float shadowFactor(vec4 fragPosLightSpace, float NdotL)
{
    vec3 proj = fragPosLightSpace.xyz / fragPosLightSpace.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z >= 1.0 ||
        proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

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

// 点光源立方体阴影：3x3x3 PCF
float pointShadowFactor(vec3 fragToLight, float fragDepth, float lightRadius)
{
    if (fragToLight == vec3(0.0))
        return 1.0;
    const float bias = lightUbo.shadowBias * 0.4;
    const float normalizedDepth = fragDepth / lightRadius;
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

vec3 acesFilm(vec3 x)
{
    x *= 0.8;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// 背景天空：从全屏 uv 重建世界方向并采样环境立方图
vec3 sampleSky()
{
    const vec2 ndc = inUV * 2.0 - 1.0;
    const vec4 world = pc.invViewProj * vec4(ndc, 1.0, 1.0);
    const vec3 dir = normalize(world.xyz / world.w - lightUbo.cameraPos);
    return texture(envMap, dir).rgb;
}

void main()
{
    const vec4 posData = subpassLoad(gPosition);
    // 背景像素：无几何，直接输出天空（已含 ACES 色调映射）
    if (posData.a <= 0.0)
    {
        outColor = vec4(acesFilm(sampleSky() * lightUbo.exposure), 1.0);
        return;
    }

    const vec4 alb = subpassLoad(gAlbedo);
    const vec4 nrm = subpassLoad(gNormal);

    const vec3 albedo = alb.rgb;
    const float metallic = clamp(alb.a, 0.0, 1.0);
    vec3 N = normalize(nrm.rgb);
    const float roughness = clamp(nrm.a, 0.045, 1.0);
    const vec3 inWorldPos = posData.rgb;

    const vec3 V = normalize(lightUbo.cameraPos - inWorldPos);
    N = faceforward(N, -V, N);

    vec3 lo = vec3(0.0);

    // ---- 方向光 + PCF 阴影 ----
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

    // ---- 点光源循环 ----
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
        if (lightUbo.lights[i].castsShadow > 0.5 && lightUbo.shadowStrength > 0.0)
        {
            const vec3 lightToFrag = normalize(-toLight);
            const float shadow = pointShadowFactor(-lightToFrag, dist, lightUbo.lights[i].radius);
            contrib *= mix(1.0, shadow, lightUbo.shadowStrength);
        }
        lo += contrib;
    }

    // ---- 环境光：IBL 与常数环境光按 IBL 强度混合 ----
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
    outColor = vec4(acesFilm(color * lightUbo.exposure), 1.0);
}
