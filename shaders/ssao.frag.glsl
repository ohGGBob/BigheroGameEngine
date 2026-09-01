#version 450
#extension GL_ARB_separate_shader_objects : enable

// SSAO 片段着色器：从 GBuffer 采样世界坐标与法线，
// 用半球核 + 随机旋转计算屏幕空间环境光遮蔽。
// 输出单通道 AO 值（1.0=无遮蔽，0.0=完全遮蔽）。

layout(set = 0, binding = 0) uniform sampler2D gPosition; // rgb=世界坐标, a=几何标记
layout(set = 0, binding = 1) uniform sampler2D gNormal;   // rgb=世界法线

layout(location = 0) in vec2 inUV;
layout(location = 0) out float outAO;

layout(push_constant) uniform PushConstants
{
    mat4  viewProj;   // 0, 64B：视投影矩阵，用于把采样点投影回屏幕
    vec3  cameraPos;  // 64, 12B：相机世界坐标，用于距离范围检查
    float radius;     // 76, 4B：采样半径（世界单位）
    float bias;       // 80, 4B：深度偏差，防止自遮蔽
    float strength;   // 84, 4B：AO 强度（幂次）
    float pad;        // 88, 4B
} pc;

const int KERNEL_SIZE = 16;
const vec3 kernel[16] = vec3[](
    vec3( 1.0,  1.0,  1.0), vec3( 1.0, -1.0,  1.0),
    vec3(-1.0,  1.0,  1.0), vec3(-1.0, -1.0,  1.0),
    vec3( 1.0,  1.0, -1.0), vec3( 1.0, -1.0, -1.0),
    vec3(-1.0,  1.0, -1.0), vec3(-1.0, -1.0, -1.0),
    vec3( 1.0,  0.0,  0.0), vec3(-1.0,  0.0,  0.0),
    vec3( 0.0,  1.0,  0.0), vec3( 0.0, -1.0,  0.0),
    vec3( 0.0,  0.0,  1.0), vec3( 0.0,  0.0, -1.0),
    vec3( 0.5,  0.5,  0.0), vec3(-0.5,  0.5,  0.0)
);

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main()
{
    const vec4 posData = texture(gPosition, inUV);
    // 背景像素：无几何，AO=1
    if (posData.a <= 0.0)
    {
        outAO = 1.0;
        return;
    }

    const vec3 worldPos = posData.rgb;
    const vec3 N = normalize(texture(gNormal, inUV).rgb);

    // 随机切向量（哈希噪声，无需噪声纹理）
    const vec3 randomVec = normalize(vec3(
        hash(inUV * 7.13) * 2.0 - 1.0,
        hash(inUV * 7.13 + 3.7) * 2.0 - 1.0,
        0.0));

    // Gram-Schmidt 构建 TBN
    const vec3 T = normalize(randomVec - N * dot(randomVec, N));
    const vec3 B = cross(N, T);
    const mat3 TBN = mat3(T, B, N);

    const float cameraDist = length(worldPos - pc.cameraPos);

    float occlusion = 0.0;
    for (int i = 0; i < KERNEL_SIZE; ++i)
    {
        // 采样点：半球核 × TBN × 半径 + 当前位置
        const vec3 samplePos = TBN * kernel[i] * pc.radius + worldPos;

        // 投影回屏幕空间
        const vec4 offset = pc.viewProj * vec4(samplePos, 1.0);
        const vec2 sampleUV = (offset.xy / offset.w) * 0.5 + 0.5;

        // 采样场景位置
        const vec3 scenePos = texture(gPosition, sampleUV).rgb;
        const float sceneDist = length(scenePos - pc.cameraPos);

        // 范围检查：采样点离相机距离在半径内才有效
        const float rangeCheck = smoothstep(0.0, 1.0, pc.radius / abs(cameraDist - sceneDist + 1e-4));
        // 深度测试：场景深度大于采样深度+偏差 → 遮蔽
        occlusion += (sceneDist >= cameraDist + pc.bias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(KERNEL_SIZE));
    outAO = clamp(pow(occlusion, pc.strength), 0.0, 1.0);
}
