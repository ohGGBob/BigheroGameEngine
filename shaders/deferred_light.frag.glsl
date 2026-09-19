#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "include/bindings.glsl"

// 延迟光照通道片段着色器：从 GBuffer 纹理采样几何信息，
// 复用与 forward 一致的 PBR/阴影/IBL 光照模型，输出最终颜色到交换链。
// 背景像素（无几何，gPosition.a==0）直接采样环境立方图作为天空。
// set3 绑定 SSAO 输出（未启用时绑定 1x1 白纹理，AO=1 无效果）。

// GBuffer 纹理（set2，不可变采样器）
layout(set = BH_SET_GBUFFER, binding = BH_GBUFFER_ALBEDO) uniform sampler2D gAlbedo;   // rgb=反照率, a=金属度
layout(set = BH_SET_GBUFFER, binding = BH_GBUFFER_NORMAL) uniform sampler2D gNormal;   // rgb=世界法线, a=粗糙度
layout(set = BH_SET_GBUFFER, binding = BH_GBUFFER_POSITION) uniform sampler2D gPosition; // rgb=世界坐标, a=几何标记

// SSAO 输出（set3）
layout(set = BH_SET_AO, binding = BH_AO_TEX) uniform sampler2D aoTex;

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
    const vec4 posData = texture(gPosition, inUV);
    // 背景像素：无几何，直接输出线性 HDR 天空（色调映射在 deferred_composite 统一执行）
    if (posData.a <= 0.0)
    {
        outColor = vec4(sampleSky() * lightUbo.exposure, 1.0);
        return;
    }

    const vec4 alb = texture(gAlbedo, inUV);
    const vec4 nrm = texture(gNormal, inUV);
    const float ao = texture(aoTex, inUV).r;

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
                const float shadow = shadowFactor(inWorldPos, NdotL);
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

    // ---- 环境光：IBL 与常数环境光按 IBL 强度混合，乘以 SSAO ----
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
    vec3 ambient = mix(constAmbient, iblAmbient, clamp(lightUbo.iblStrength, 0.0, 1.0));
    ambient *= ao; // SSAO 仅影响环境光项

    const vec3 color = lo + ambient;
    // 输出线性 HDR：曝光在此统一相乘（延迟链无自动曝光），ACES 色调映射由
    // deferred_composite 在链路末端执行（0.17.9 双重 ACES 修复）
    outColor = vec4(color * lightUbo.exposure, 1.0);
}
