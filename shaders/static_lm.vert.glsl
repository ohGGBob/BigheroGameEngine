#version 450
#extension GL_ARB_separate_shader_objects : enable

// 静态光照贴图批次顶点着色器（U2-L1 渲染接线 v1；0.22.37 扩展输出）：
// 合并静态批次 = 世界空间顶点（构建时烘焙坐标），相机 UBO 直接变换。
// 顶点输入只含批次管线实际消费的子集（pos / normal / color / lmUV；tangent 不声明）。
// 输出三组 varying：前向 static_lm.frag 只消费 lmUV；延迟 GBuffer 变体
// （deferred_static_lm.frag）额外消费 normal / worldPos 写 MRT。

#include "include/bindings.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inLmUV;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec3 inColor;

layout(set = BH_SET_CAMERA, binding = BH_CAMERA_UBO, std140) uniform CameraUBO
{
    mat4 view;
    mat4 proj;
} uboCamera;

layout(location = 0) out vec2 outLmUV;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec3 outColor;

void main()
{
    gl_Position = uboCamera.proj * uboCamera.view * vec4(inPos, 1.0);
    outLmUV = inLmUV;
    outNormal = inNormal;
    outWorldPos = inPos;
    outColor = inColor;
}
