#version 450
#extension GL_ARB_separate_shader_objects : enable

// 静态光照贴图批次片段着色器（U2-L1 渲染接线 v1）：
// 光照贴图 = 出射辐射度（albedo/直接光/天光 AO 已在烘焙时吸收），直接输出，
// 不与实时直接光/环境光叠加（与 Unity lightmap 静态几何语义一致）。
// v1 范围：无 IBL specular、无 CSM/点光实时项（批次替代实时路径的静态光照）。

#include "include/bindings.glsl"

layout(location = 0) in vec2 inLmUV;
layout(location = 0) out vec4 outColor;

layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_LIGHTMAP_ATLAS) uniform sampler2D lmAtlas;

void main()
{
    outColor = vec4(texture(lmAtlas, inLmUV).rgb, 1.0);
}
