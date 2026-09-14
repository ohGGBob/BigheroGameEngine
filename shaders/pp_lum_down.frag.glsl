#version 450
// 自动曝光 2/3：对数亮度 8x8 盒式下采样（64→8→1 共用，缩减比恒为 8）。
// 源图边长经 push constant 传入（64 或 8），据此推算足迹宽度。
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uSrc;

layout(push_constant) uniform Params
{
    float srcSize; // 源图边长（64 或 8）
    float pad0;
    float pad1;
    float pad2;
} params;

void main()
{
    // 目标纹素对应源图 8x8 块；足迹 UV 宽度 = 8 / srcSize
    const float footprint = 8.0 / params.srcSize;
    float sum = 0.0;
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const vec2 off = footprint * ((vec2(x, y) + 0.5) / 8.0 - 0.5);
            sum += texture(uSrc, inUv + off).r;
        }
    }
    outColor = vec4(sum / 64.0, 0.0, 0.0, 1.0);
}
