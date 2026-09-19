#version 450
// 运行时 UI 批渲染顶点着色器：屏幕像素坐标（左上原点，y 向下）经推送常量仿射到 NDC。
// 单一大顶点缓冲容纳全部矩形/文本四边形，单 draw call。
layout(location = 0) in vec2 inPos;    // 屏幕像素
layout(location = 1) in vec2 inLocal;  // 相对矩形中心的偏移（像素，圆角 SDF 用）
layout(location = 2) in vec4 inColor;  // 顶点色 RGBA 0-1
layout(location = 3) in vec2 inUv;     // 图集 UV
layout(location = 4) in vec4 inRect;   // halfW, halfH, cornerRadius(px), 未用

layout(push_constant) uniform PC
{
    vec4 scaleOffset; // xy = (2/w, 2/h)，zw = (-1, -1)
} u;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUv;
layout(location = 2) out vec2 outLocal;
layout(location = 3) out vec4 outRect;

void main()
{
    gl_Position = vec4(inPos * u.scaleOffset.xy + u.scaleOffset.zw, 0.0, 1.0);
    outColor = inColor;
    outUv = inUv;
    outLocal = inLocal;
    outRect = inRect;
}
