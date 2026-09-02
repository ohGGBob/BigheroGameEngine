#version 450
#extension GL_ARB_separate_shader_objects : enable

// 延迟合成 Pass：将离屏场景颜色与 SSR 反射混合，输出到交换链。
// SSR 关闭时 reflection 采样为黑色，等效于直接复制场景颜色。

layout(set = 0, binding = 0) uniform sampler2D sceneColor; // 离屏光照颜色
layout(set = 0, binding = 1) uniform sampler2D reflection; // SSR 反射（rgb=颜色, a=强度）

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
    float ssrStrength; // 0, 4B：反射强度倍率
    float pad0;
    float pad1;
    float pad2;
}
pc;

void main()
{
    vec3 color = texture(sceneColor, inUV).rgb;
    vec4 refl = texture(reflection, inUV);

    // 反射已经在 ray pass 中乘以了命中强度/菲涅尔/粗糙度，
    // 这里再乘全局强度，然后简单叠加到场景颜色上
    color += refl.rgb * pc.ssrStrength;

    outColor = vec4(color, 1.0);
}
