#version 450
#extension GL_ARB_separate_shader_objects : enable

// 静态光照贴图批次顶点着色器（U2-L1 渲染接线 v1）：
// 合并静态批次 = 世界空间顶点（构建时烘焙坐标），相机 UBO 直接变换。
// 顶点输入只含批次实际消费的子集（pos + lmUV；批次顶点结构中其余字段不声明）。

#include "include/bindings.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inLmUV;

layout(set = BH_SET_CAMERA, binding = BH_CAMERA_UBO, std140) uniform CameraUBO
{
    mat4 view;
    mat4 proj;
} uboCamera;

layout(location = 0) out vec2 outLmUV;

void main()
{
    gl_Position = uboCamera.proj * uboCamera.view * vec4(inPos, 1.0);
    outLmUV = inLmUV;
}
