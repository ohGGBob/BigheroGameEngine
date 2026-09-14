#version 450
// 自动曝光 3/3：亮度适应（1x1 ping-pong）。
// new = prev + (avg - prev) × (1 - exp(-dt × speed))，即指数趋近当前帧平均对数亮度，
// 模拟人眼明暗适应的延迟。首帧（reset=1）直接取当前均值，避免未定义初始值。
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uCurrentAvg;   // 本帧平均对数亮度（1x1）
layout(set = 0, binding = 1) uniform sampler2D uPrevAdapted;  // 上一帧适应结果（1x1）

layout(push_constant) uniform Params
{
    float factor; // 1 - exp(-dt × adaptationSpeed)，CPU 端预算
    float reset;  // 1=首帧（忽略 prev 直接取 avg）
    float pad0;
    float pad1;
} params;

void main()
{
    const float avg = texture(uCurrentAvg, vec2(0.5)).r;
    const float prev = texture(uPrevAdapted, vec2(0.5)).r;
    const float smoothed = prev + (avg - prev) * params.factor;
    outColor = vec4(mix(smoothed, avg, params.reset), 0.0, 0.0, 1.0);
}
