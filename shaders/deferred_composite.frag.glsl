#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "include/bindings.glsl"

// 延迟合成 Pass：将离屏场景颜色与 SSR 反射混合，输出到交换链。
// SSR 关闭时 reflection 采样为黑色，等效于直接复制场景颜色。
// 场景颜色为线性 HDR（曝光已在 deferred_light 相乘），此处执行链路末端唯一的
// ACES 色调映射后输出——与 pp_composite 在前向链中的职责对应（0.17.9）。

layout(set = BH_SET_POST, binding = BH_PP_SLOT0) uniform sampler2D sceneColor; // 离屏光照颜色
layout(set = BH_SET_POST, binding = BH_PP_SLOT1) uniform sampler2D reflection; // SSR 反射（rgb=颜色, a=强度）

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

// ACES 近似色调映射（Narkowicz），与 pp_composite 同款公式
vec3 acesTonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 color = texture(sceneColor, inUV).rgb;
    vec4 refl = texture(reflection, inUV);

    // 反射已经在 ray pass 中乘以了命中强度/菲涅尔/粗糙度，
    // 这里再乘全局强度，然后简单叠加到场景颜色上
    color += refl.rgb * pc.ssrStrength;

    outColor = vec4(acesTonemap(color), 1.0);
}
