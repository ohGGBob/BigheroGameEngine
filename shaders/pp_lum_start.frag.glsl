#version 450
// 自动曝光 1/3：场景 HDR 颜色 → 64x64 对数亮度图。
// 每个目标纹素在其对应的屏幕区域内取 4x4 双线性网格采样，
// 输出该区域平均对数亮度（R 通道），供逐级盒式下采样收敛到 1x1。
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;

const float kTarget = 64.0; // 目标图边长（与 PostProcessor 的 lum64 尺寸一致）

float luminance(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    const float kMinLum = 1e-4; // 纯黑区域对数下限，防 log(0) 与曝光爆炸
    float sum = 0.0;
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            // 采样点覆盖本纹素的屏幕足迹：足迹边长 = 1/64（UV 空间）
            const vec2 off = ((vec2(x, y) + 0.5) / 4.0 - 0.5) / kTarget;
            const vec3 c = texture(uScene, inUv + off).rgb;
            sum += log(max(luminance(c), kMinLum));
        }
    }
    outColor = vec4(sum / 16.0, 0.0, 0.0, 1.0);
}
