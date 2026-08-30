#version 450
// BRDF LUT：分裂求和近似的环境BRDF积分项（NdotV x 粗糙度）

layout(location = 0) in vec3 inDir;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979;

float radicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 xi, vec3 N, float roughness)
{
    const float a = roughness * roughness;
    const float phi = 2.0 * PI * xi.x;
    const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    const vec3 up = (abs(N.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    const vec3 tangent = normalize(cross(up, N));
    const vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

// 几何项（与主PBR着色器一致的Smith-Schlick）
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

vec2 integrateBRDF(float NdotV, float roughness)
{
    const vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    const vec3 N = vec3(0.0, 0.0, 1.0);

    float A = 0.0;
    float B = 0.0;
    const uint sampleCount = 1024u;

    for (uint i = 0u; i < sampleCount; ++i)
    {
        const vec2 xi = hammersley(i, sampleCount);
        const vec3 H = importanceSampleGGX(xi, N, roughness);
        const vec3 L = normalize(2.0 * dot(V, H) * H - V);

        const float NdotL = max(L.z, 0.0);
        const float NdotH = max(H.z, 0.0);
        const float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0)
        {
            const float G = geometrySmith(N, V, L, roughness);
            const float Gvis = (G * VdotH) / max(NdotH * NdotV, 1e-7);
            const float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * Gvis;
            B += Fc * Gvis;
        }
    }

    return vec2(A, B) / float(sampleCount);
}

void main()
{
    outColor = vec4(integrateBRDF(inUV.x, inUV.y), 0.0, 1.0);
}
