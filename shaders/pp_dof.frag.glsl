#version 450
// 景深（DoF）：黄金角圆盘采集（gather），依据线性深度计算弥散圆（CoC），
// 背景/前景均产生合理虚化。enabled=0 时直通原图（不改变画面观感）。
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;   // HDR 场景颜色（已 resolve）
layout(set = 0, binding = 1) uniform sampler2D linearDepth;  // R32F 正向视线距离

layout(push_constant) uniform Params
{
    float focusDistance; // 对焦距离（世界单位，与线性深度同量纲）
    float aperture;      // 弥散圆强度（光圈/焦距，越大越虚）
    float maxBlur;       // 最大模糊半径（UV 空间，如 0.02）
    float enabled;       // 1=启用，0=直通
} params;

const int SAMPLES = 32;
const float GOLDEN_ANGLE = 2.39996323;

float circleOfConfusion(vec2 uv)
{
    float d = texture(linearDepth, uv).r;
    return clamp(abs(d - params.focusDistance) * params.aperture, 0.0, params.maxBlur);
}

void main()
{
    vec3 center = texture(sceneColor, inUv).rgb;
    if (params.enabled < 0.5)
    {
        outColor = vec4(center, 1.0);
        return;
    }

    float radius = circleOfConfusion(inUv);
    if (radius < 0.0009)
    {
        outColor = vec4(center, 1.0);
        return;
    }

    vec3 acc = center;
    float wsum = 1.0;
    for (int i = 0; i < SAMPLES; ++i)
    {
        float t = (float(i) + 0.5) / float(SAMPLES);
        float ang = float(i) * GOLDEN_ANGLE;
        vec2 offs = vec2(cos(ang), sin(ang)) * sqrt(t) * radius;
        vec2 uv = inUv + offs;
        vec3 c = texture(sceneColor, uv).rgb;
        float tcoc = circleOfConfusion(uv);
        // 仅当邻域像素的弥散圆覆盖当前像素时计入（前景/背景 bleed）
        float w = (tcoc * tcoc >= dot(offs, offs)) ? 1.0 : 0.0;
        acc += c * w;
        wsum += w;
    }

    outColor = vec4(acc / max(wsum, 1e-4), 1.0);
}
