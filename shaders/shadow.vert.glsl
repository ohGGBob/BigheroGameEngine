#version 450
#extension GL_ARB_separate_shader_objects : enable

// 阴影深度预通道顶点着色器：只关心光照视空间深度
layout(location = 0) in vec3 inPos;
// 位置/法线/UV/颜色/切线——仅消费位置，其余属性由管线声明但着色器可忽略

layout(push_constant) uniform PushShadow {
    mat4 lightSpace;  // 光源视投影矩阵
    mat4 model;       // 物体模型矩阵
} pushShadow;

void main()
{
    gl_Position = pushShadow.lightSpace * pushShadow.model * vec4(inPos, 1.0);
}
