#version 450
#extension GL_ARB_separate_shader_objects : enable

// 延迟 GBuffer 静态批次片段着色器（0.22.37，U2-L1 deferred 补全）：
// 静止批次在几何子通道写 4 张 MRT——albedo/法线/世界坐标照常（供 SSAO/SSR 等下游
// 消费真实几何数据），烘焙辐射度写入第 4 附件（rgb=光照贴图采样，a=标记 1）。
// 延迟光照 Pass 据标记直接输出辐射度，跳过实时 PBR——与前向 static_lm 的
// 「光照贴图 = 完整出射辐射度」语义逐位一致（线性 HDR，曝光/ACES 由合成端统一处理）。
// 粗糙度槽写 1.0（全粗糙）：SSR 合成端虽无条件叠加反射，但 fresnelSchlickRoughness
// 在 roughness=1 下 F0→Fmax、(1-cos)^5→0，镜面贡献趋零，静态物不产生实时反射叠加。

#include "include/bindings.glsl"

layout(location = 0) in vec2 inLmUV;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec3 inColor;

layout(set = BH_SET_MATERIAL, binding = BH_MATERIAL_LIGHTMAP_ATLAS) uniform sampler2D lmAtlas;

layout(location = 0) out vec4 outAlbedo;   // rgb = 批次反照率, a = 金属度 0
layout(location = 1) out vec4 outNormal;   // rgb = 世界法线, a = 粗糙度 1（全粗糙）
layout(location = 2) out vec4 outPosition; // rgb = 世界坐标, a = 1（几何标记）
layout(location = 3) out vec4 outLm;       // rgb = 烘焙辐射度（线性 HDR）, a = 1（静态标记）

void main()
{
    const vec3 N = normalize(inNormal);
    const vec3 lm = texture(lmAtlas, inLmUV).rgb;

    outAlbedo = vec4(inColor, 0.0);
    outNormal = vec4(N, 1.0);
    outPosition = vec4(inWorldPos, 1.0);
    outLm = vec4(lm, 1.0);
}
