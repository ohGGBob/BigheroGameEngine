#version 450
// 预滤波镜面环境：GGX重要性采样，按粗糙度写入对应mip级

layout(set = 0, binding = 0) uniform samplerCube envMap;

// 与顶点阶段同一块推送常量，这里读取预滤波粗糙度
layout(push_constant) uniform PushFace {
    mat4 basis;
    float roughness;
    float pad0;
    float pad1;
    float pad2;
} pushFace;

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

void main()
{
    const vec3 N = normalize(inDir);
    const vec3 V = N;
    const float roughness = clamp(pushFace.roughness, 0.0, 1.0);

    const uint sampleCount = 1024u;
    vec3 prefilteredColor = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < sampleCount; ++i)
    {
        const vec2 xi = hammersley(i, sampleCount);
        const vec3 H = importanceSampleGGX(xi, N, roughness);
        const vec3 L = normalize(2.0 * dot(V, H) * H - V);

        const float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            prefilteredColor += texture(envMap, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    outColor = vec4(prefilteredColor / max(totalWeight, 1e-4), 1.0);
}
