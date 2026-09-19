#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "include/bindings.glsl"

// 延迟合成 Pass：将离屏线性 HDR 光照颜色与 SSR 反射混合，统一 ACES 色调映射后输出交换链。
// SSR 关闭时 reflection 采样为黑色，等效于直接复制场景颜色。

layout(set = BH_SET_POST, binding = BH_PP_SLOT0) uniform sampler2D sceneColor; // 离屏光照颜色（线性 HDR）
layout(set = BH_SET_POST, binding = BH_PP_SLOT1) uniform sampler2D reflection; // SSR 反射（rgb=颜色, a=强度）

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants
{
    float ssrStrength; // 0, 4B：反射强度倍率
    float exposure;    // 4, 4B：曝光（与 PP 合成端同源，链末端统一乘）
    float pad0;
    float pad1;
}
pc;

// 链末端唯一曝光+色调映射点：与 forward+PP 路径的合成端一致，避免光在 HDR
// 离屏图里被提前压缩（延迟光照 Pass 输出纯线性域，不乘曝光、不做 ACES）。
vec3 acesFilm(vec3 x)
{
    x *= 0.8;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec3 color = texture(sceneColor, inUV).rgb;
    vec4 refl = texture(reflection, inUV);

    // 反射已经在 ray pass 中乘以了命中强度/菲涅尔/粗糙度，
    // 这里再乘全局强度，然后简单叠加到场景颜色上
    color += refl.rgb * pc.ssrStrength;

    outColor = vec4(acesFilm(color * pc.exposure), 1.0);
}
