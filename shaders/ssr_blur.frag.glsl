#version 450
#extension GL_ARB_separate_shader_objects : enable

// SSR 模糊：可分离高斯模糊（水平/垂直由 push constant 控制），
// 对反射缓冲做平滑，消除 ray march 的噪点。

layout(set = 0, binding = 0) uniform sampler2D inputTex;

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
    vec2 texelSize; // 0, 8B：1/纹理尺寸
    int direction;  // 8, 4B：0=水平, 1=垂直
    float pad;      // 12
}
pc;

const float WEIGHTS[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main()
{
    vec2 offset = (pc.direction == 0) ? vec2(pc.texelSize.x, 0.0) : vec2(0.0, pc.texelSize.y);

    vec4 result = texture(inputTex, inUV) * WEIGHTS[0];
    for (int i = 1; i < 5; ++i)
    {
        result += texture(inputTex, inUV + offset * float(i)) * WEIGHTS[i];
        result += texture(inputTex, inUV - offset * float(i)) * WEIGHTS[i];
    }
    outColor = result;
}
