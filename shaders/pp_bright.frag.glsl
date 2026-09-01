#version 450
// 亮部提取 Pass：采样场景颜色，输出超过阈值的亮部到半分辨率缓冲
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform BrightParams
{
    float threshold;
    float softKnee;
} params;

void main()
{
    vec3 color = texture(uScene, inUv).rgb;
    float brightness = max(max(color.r, color.g), color.b);

    // Soft knee 阈值：亮部平滑过渡，避免硬边
    float knee = params.threshold * params.softKnee;
    float soft = brightness - params.threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, brightness - params.threshold);
    contribution /= max(brightness, 1e-5);

    outColor = vec4(color * contribution, 1.0);
}
