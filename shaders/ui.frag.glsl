#version 450
// 运行时 UI 批渲染片段着色器：
//   - 图集单通道 R8：text 纹素 = 字形 alpha；纯色矩形采样 (0,0) 保留白像素（r=1）直出；
//   - 圆角：inRect.z > 0 时以 SDF 有符号距离裁剪 coverage（fwidth 抗锯齿）。
layout(set = 0, binding = 0) uniform sampler2D uAtlas;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec2 inLocal;
layout(location = 3) in vec4 inRect;

layout(location = 0) out vec4 outColor;

void main()
{
    float coverage = 1.0;
    if (inRect.z > 0.0)
    {
        // 圆角矩形 SDF：q = |p| - (half - r)，d<0 在圆角内部
        vec2 halfSize = inRect.xy;
        vec2 q = abs(inLocal) - (halfSize - vec2(inRect.z));
        float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - inRect.z;
        float aa = max(fwidth(d), 1e-4);
        coverage = 1.0 - smoothstep(-aa, aa, d);
    }
    float a = texture(uAtlas, inUv).r;
    outColor = vec4(inColor.rgb, inColor.a * a * coverage);
}
