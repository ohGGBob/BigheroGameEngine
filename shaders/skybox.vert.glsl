#version 450
// 天空盒顶点：全屏三角形打到远平面，重建世界方向

layout(push_constant) uniform PushSky {
    mat4 invViewProj; // 相机 view*proj 的逆矩阵
} pushSky;

layout(location = 0) out vec3 outPoint;

void main()
{
    const vec2 uv = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    const vec4 ndc = vec4(uv * 2.0 - 1.0, 1.0, 1.0); // 远平面深度
    gl_Position = ndc;

    const vec4 world = pushSky.invViewProj * ndc;
    outPoint = world.xyz / world.w;
}
