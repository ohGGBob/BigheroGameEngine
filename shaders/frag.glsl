#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inVertColor;
layout(location = 4) in vec3 inTangent;

layout(location = 0) out vec4 outColor;

// set1 binding0：光照/环境参数；binding1：反照率纹理；binding2：法线贴图
layout(set = 1, binding = 0, std140) uniform LightUBO {
    vec3 lightDir;
    float intensity;
    vec3 lightColor;
    float ambientFactor;
    vec3 cameraPos;
    float padding;
} lightUbo;

layout(set = 1, binding = 1) uniform sampler2D albedoTex;
layout(set = 1, binding = 2) uniform sampler2D normalTex;

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
    N = faceforward(N, -normalize(lightUbo.cameraPos - inWorldPos), N);

    vec3 V = normalize(lightUbo.cameraPos - inWorldPos);
    vec3 L = normalize(-lightUbo.lightDir);
    vec3 H = normalize(V + L);

    // ---- 材质参数 ----
    // 反照率 = 顶点色 x 纹理（SRGB纹理硬件已转线性）
    const vec3 albedo = inVertColor * texture(albedoTex, inUV).rgb;
    const float metallic = clamp(pushObject.metallic, 0.0, 1.0);
    const float roughness = clamp(pushObject.roughness, 0.045, 1.0);

    // F0：电介质默认0.04，金属用反照率
    const vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // ---- Cook-Torrance双向反射分布函数 ----
    const float NdotL = max(dot(N, L), 0.0);
    const vec3 radiance = lightUbo.lightColor * lightUbo.intensity;

    const float D = distributionGGX(N, H, roughness);
    const float G = geometrySmith(N, V, L, roughness);
    const vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    const vec3 specular = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 1e-7);
    const vec3 kd = (1.0 - F) * (1.0 - metallic);

    vec3 lo = (kd * albedo / PI + specular) * radiance * NdotL;

    // ---- 环境光（常数项，无IBL）：电介质直接作用反照率，金属按F0着色 ----
    const vec3 ambientTint = mix(vec3(1.0), F0, metallic);
    const vec3 ambient = lightUbo.ambientFactor * albedo * ambientTint;
    vec3 color = lo + ambient;

    // ACES色调映射后输出线性颜色（sRGB交换链负责编码）
    outColor = vec4(acesFilm(color), 1.0);
}
