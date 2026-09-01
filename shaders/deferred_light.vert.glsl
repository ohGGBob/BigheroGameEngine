#version 450
#extension GL_ARB_separate_shader_objects : enable

// 延迟光照通道全屏三角形：无顶点缓冲，凭 gl_VertexIndex 生成覆盖全屏的三角形。
// 输出 uv（0..1 覆盖屏幕）供片段着色器重建背景天空方向。
layout(location = 0) out vec2 outUV;

void main()
{
    // 三个顶点覆盖 [-1,3] 区间，恰好铺满裁剪空间 [-1,1]
    const vec2 pos = vec2(
        (gl_VertexIndex == 1) ? 3.0f : -1.0f,
        (gl_VertexIndex == 2) ? 3.0f : -1.0f);
    outUV = pos * 0.5f + 0.5f;
    gl_Position = vec4(pos, 0.0f, 1.0f);
}
