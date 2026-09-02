#version 450
// 合成 Pass：将 HDR 场景颜色与模糊亮部相加，执行 ACES 色调映射，输出到交换链
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uBloom;

layout(push_constant) uniform CompositeParams
{
    float bloomStrength;
    float exposure;
    float saturation;
    float contrast;
    float lift;
    float gain;
    float gamma;
} params;

// ACES 电影级色调映射（Narkowicz 近似）
vec3 acesTonemap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// 色调分级（与 CPU render/ColorGrading.h 的 GradeColor 同一公式）
vec3 gradeColor(vec3 c)
{
    vec3 x = c * params.gain + vec3(params.lift);
    x = max(x, vec3(0.0));
    x = pow(x, vec3(params.gamma));
    x = (x - vec3(0.5)) * params.contrast + vec3(0.5);
    float luma = dot(x, vec3(0.2126, 0.7152, 0.0722));
    x = mix(vec3(luma), x, params.saturation);
    return max(x, vec3(0.0));
}

void main()
{
    vec3 scene = texture(uScene, inUv).rgb;
    vec3 bloom = texture(uBloom, inUv).rgb;

    vec3 color = scene + bloom * params.bloomStrength;
    color *= params.exposure;
    color = acesTonemap(color);

    // 升级 21：色调分级（在 ACES 之后作用于显示参考颜色）
    color = gradeColor(color);

    // 伽马校正（交换链为 sRGB 格式时由硬件完成，此处输出线性）
    outColor = vec4(color, 1.0);
}
