#version 450
// 高斯模糊 Pass：9 抽头分离式高斯模糊，方向由 push constant 控制（水平/垂直）
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uInput;

layout(push_constant) uniform BlurParams
{
    vec2 direction; // (1/width, 0) 水平 或 (0, 1/height) 垂直
} params;

void main()
{
    // 9-tap 高斯权重（sigma ≈ 2.0）
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

    vec3 result = texture(uInput, inUv).rgb * weights[0];
    for (int i = 1; i < 5; ++i)
    {
        vec2 offset = params.direction * float(i);
        result += texture(uInput, inUv + offset).rgb * weights[i];
        result += texture(uInput, inUv - offset).rgb * weights[i];
    }

    outColor = vec4(result, 1.0);
}
