#version 450
// IBL卷积/全屏通道公用顶点着色器：全屏三角形，输出面方向与UV
// 面基矩阵列：s方向 / t方向 / 主轴，与CPU立方图生成共用同一约定

layout(push_constant) uniform PushFace {
    mat4 basis;      // 列：sVec / tVec / major
    float roughness; // 预滤波用，其他通道忽略
    float pad0;
    float pad1;
    float pad2;
} pushFace;

layout(location = 0) out vec3 outDir;
layout(location = 1) out vec2 outUV;

void main()
{
    const vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    outUV = uv;
    outDir = mat3(pushFace.basis) * vec3(2.0 * uv.x - 1.0, 2.0 * uv.y - 1.0, 1.0);
}
