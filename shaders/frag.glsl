#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "include/bindings.glsl"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inVertColor;
layout(location = 4) in vec3 inTangent;
// 材质参数（原经推送常量，现由逐实例输入经顶点阶段传递）
layout(location = 5) in float inMetallic;
layout(location = 6) in float inRoughness;
layout(location = 7) in float inVertAlpha;

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

layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_LIGHT_UBO, std140) uniform LightUBO {
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
    mat4 lightSpaceMatrices[4]; // 级联阴影：每级联一个正交光视矩阵
    vec4 cascadeSplits;         // 轴向视图深度分割边界：级联 c 覆盖 (c>0?splits[c-1]:0, splits[c]]
    vec4 cameraForward;         // xyz=相机前向单位向量（轴向深度度量），w=阴影最远绘制距离
    PointLight lights[8];
} lightUbo;

layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_ALBEDO_TEX) uniform sampler2D albedoTex;
layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_NORMAL_TEX) uniform sampler2D normalTex;
layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_SHADOW_MAP) uniform sampler2D shadowMap;
layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_ENV_MAP) uniform samplerCube envMap;
layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_IRRADIANCE_MAP) uniform samplerCube irradianceMap;
layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_PREFILTERED_MAP) uniform samplerCube prefilteredMap;
layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_BRDF_LUT) uniform sampler2D brdfLut;
layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_POINT_SHADOW_MAP) uniform samplerCube pointShadowMap;
// 逐物体纹理池（16槽）：索引来自推送常量（动态均匀），每材质一次绘制无需 nonuniformEXT
layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_OBJECT_TEX) uniform sampler2D uObjectTex[16];

// 逐材质推送常量：纹理池槽位 + 透明/自发光参数（glTF 2.0 alphaMode 语义）
// mode：0=OPAQUE 1=MASK 2=BLEND 3=EMISSIVE_ONLY（延迟自发光叠加，加性混合管线专用）
layout(push_constant) uniform ObjectPush
{
    int texIndex;        // 反照率贴图槽
    int normalIndex;     // 法线贴图槽
    int mrIndex;         // metallicRoughness 贴图槽（无贴图时指向纯白：g=b=1 透传因子）
    int emissiveIndex;   // 自发光贴图槽（-1=无贴图，emissiveFactor 原样生效）
    vec3 emissiveFactor; // 自发光倍率（线性 HDR）
    float alphaCutoff;   // MASK 裁剪阈值（<=0 视为不透明）
    int mode;            // 渲染模式（见上）
} objPush;

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

// ---- 级联阴影（CSM）：4 级联 2x2 图集，轴向视深选级 + 级间渐变混合 ----
const int kCascades = 4;

// 单级联 3x3 PCF：级联矩阵变换 + 平铺到图集子块（级联 c -> 列 c&1、行 c/2）
float pcfCascade(vec3 worldPos, float NdotL, int cascade)
{
    const vec4 lp = lightUbo.lightSpaceMatrices[cascade] * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    proj = proj * 0.5 + 0.5;

    const vec2 tileOffset = vec2(float(cascade & 1), float(cascade / 2)) * 0.5;
    proj.xy = proj.xy * 0.5 + tileOffset;

    // 光照视锥体之外（z 越界或溢出子块 XY 覆盖）不受阴影
    if (proj.z >= 1.0 ||
        any(lessThan(proj.xy, tileOffset)) || any(greaterThan(proj.xy, tileOffset + vec2(0.5))))
        return 1.0;

    // 斜率比例偏移：掠射角加大偏移
    const float bias = max(lightUbo.shadowBias * (1.0 - NdotL), lightUbo.shadowBias * 0.1);

    const vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    // PCF 核收拢在子块内，防止采样跨块混叠
    const vec2 lo = tileOffset + texelSize;
    const vec2 hi = tileOffset + vec2(0.5) - texelSize;
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            const vec2 uv = clamp(proj.xy, lo, hi) + vec2(x, y) * texelSize;
            const float sampled = texture(shadowMap, uv).r;
            shadow += (proj.z - bias > sampled) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

// 级联选择 + 采样：轴向视深 = 相机前向·(片元-相机)，级联深度带后 20% 渐变到下一级
float shadowFactor(vec3 worldPos, float NdotL)
{
    const float viewDepth = dot(lightUbo.cameraForward.xyz, worldPos - lightUbo.cameraPos);
    if (viewDepth >= lightUbo.cameraForward.w)
        return 1.0; // 超出阴影绘制距离

    int cascade = kCascades - 1;
    for (int i = 0; i < kCascades; ++i)
    {
        if (viewDepth <= lightUbo.cascadeSplits[i])
        {
            cascade = i;
            break;
        }
    }

    const float shadow = pcfCascade(worldPos, NdotL, cascade);
    if (cascade + 1 < kCascades)
    {
        const float lo = cascade > 0 ? lightUbo.cascadeSplits[cascade - 1] : 0.0;
        const float t = (viewDepth - lo) / max(lightUbo.cascadeSplits[cascade] - lo, 1e-4);
        const float blend = smoothstep(0.8, 1.0, t);
        if (blend > 0.0)
            return mix(shadow, pcfCascade(worldPos, NdotL, cascade + 1), blend);
    }
    return shadow;
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

void main()
{
    // ---- TBN构建与法线贴图 ----
    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent - N * dot(N, inTangent)); // Gram-Schmidt正交化
    vec3 B = cross(N, T);
    const mat3 TBN = mat3(T, B, N);

    vec3 mapped = texture(uObjectTex[objPush.normalIndex], inUV).xyz * 2.0 - 1.0;
    N = normalize(TBN * mapped);
    // 防止法线背向视线导致的负值光照
    const vec3 V = normalize(lightUbo.cameraPos - inWorldPos);
    N = faceforward(N, -V, N);

    // ---- 材质参数 ----
    // 反照率 = 顶点色(tint) × 反照率贴图；alpha = 顶点 alpha(tint.w) × 贴图 alpha；
    // 金属度/粗糙度 = 因子 × metallicRoughness 贴图通道
    // （glTF 2.0 约定：mr 贴图 G=粗糙度 B=金属度；无贴图时指向纯白槽，因子原样透传）
    const vec4 baseTex = texture(uObjectTex[objPush.texIndex], inUV);
    const vec3 albedo = inVertColor * baseTex.rgb;
    const float alpha = inVertAlpha * baseTex.a;
    const vec4 mrSample = texture(uObjectTex[objPush.mrIndex], inUV);
    const float metallic = clamp(inMetallic * mrSample.b, 0.0, 1.0);
    const float roughness = clamp(inRoughness * mrSample.g, 0.045, 1.0);

    // glTF 透明：MASK 按阈值裁剪（BLEND 不裁剪，alpha 经管线混合输出）
    if (objPush.mode == 1 && alpha < objPush.alphaCutoff)
        discard;

    // ---- 自发光：因子 × 贴图（线性 HDR，不受光照调制） ----
    vec3 emissive = objPush.emissiveFactor;
    if (objPush.emissiveIndex >= 0)
        emissive *= texture(uObjectTex[objPush.emissiveIndex], inUV).rgb;

    // 延迟自发光叠加：仅输出自发光项（加性混合管线写回光照结果，alpha 通道不写入）。
    // 输出线性 HDR：色调映射/曝光由链路末端统一执行（0.17.9 双重 ACES 修复）
    if (objPush.mode == 3)
    {
        outColor = vec4(emissive, 0.0);
        return;
    }

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
                const float shadow = shadowFactor(inWorldPos, NdotL);
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
    const vec3 color = lo + ambient + emissive;

    // 输出线性 HDR（曝光与 ACES 色调映射由链路末端 pp_composite/deferred_composite
    // 统一执行；片元端双重 ACES 会把暗部压至 ~1/4 并使曝光滑条平方生效，0.17.9 修复）；
    // BLEND 透传几何 alpha
    outColor = vec4(color, (objPush.mode == 2) ? alpha : 1.0);
}
