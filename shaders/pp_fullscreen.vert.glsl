#version 450
// 全屏三角形顶点着色器：无顶点缓冲，用 gl_VertexIndex 生成覆盖屏幕的三角形
// 三个顶点坐标：(-1,-1), (3,-1), (-1,3)，覆盖整个视口
layout(location = 0) out vec2 outUv;

void main()
{
    vec2 pos = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    outUv = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
