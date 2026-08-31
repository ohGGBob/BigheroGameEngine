#version 450
#extension GL_ARB_separate_shader_objects : enable

// 点光源立方体阴影深度预通道顶点着色器：
// 6 个面的视投影矩阵位于 set=2 binding=0 的 PointShadowUBO（std140 数组）。
// std140 数组下标必须使用常量表达式（dynamic indexing 不合法），
// 故以 push_constant 的 face 索引经 if-else 选择对应面的矩阵，其余项置零。
layout(location = 0) in vec3 inPos;

layout(set = 2, binding = 0, std140) uniform PointShadowUBO {
    mat4 faceMatrices[6]; // 顺序：+X,-X,+Y,-Y,+Z,-Z
} pointShadowUbo;

layout(push_constant) uniform PushCubeShadow {
    mat4 model;        // 物体模型矩阵
    vec4 faceIndex;    // 当前面索引（取 x 分量），立方体 0..5
} push;

void main()
{
    const int face = int(push.faceIndex.x);
    mat4 m = mat4(0.0);
    if (face == 0) m = pointShadowUbo.faceMatrices[0];
    else if (face == 1) m = pointShadowUbo.faceMatrices[1];
    else if (face == 2) m = pointShadowUbo.faceMatrices[2];
    else if (face == 3) m = pointShadowUbo.faceMatrices[3];
    else if (face == 4) m = pointShadowUbo.faceMatrices[4];
    else m = pointShadowUbo.faceMatrices[5];

    gl_Position = m * push.model * vec4(inPos, 1.0);
}
