#version 450
// 粒子公告板顶点着色器：以 gl_VertexIndex 生成单位四边形，
// 用 camRight/camUp 世界轴展开为面向相机的公告板（billboard）。
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inSize;

layout(push_constant) uniform PC
{
    mat4 viewProj;
    vec3 camRight;
    float _p0;
    vec3 camUp;
    float _p1;
} u;

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outUV;

void main()
{
    // 两三角形覆盖单位正方形（中心在原点，边 [-1,1]）
    vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
        vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 c = corners[gl_VertexIndex];
    vec3 world = inPosition + (u.camRight * c.x + u.camUp * c.y) * inSize;
    gl_Position = u.viewProj * vec4(world, 1.0);
    outColor = inColor;
    outUV = c;
}
