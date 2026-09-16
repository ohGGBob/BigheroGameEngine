#pragma once
#include <cstdint>

// ============================================================================
// 中央描述符集/绑定常量（阶段一 · 1.3）—— 着色器侧契约的 C++ 镜像
//
// 本文件数值 MUST 与 `shaders/include/bindings.glsl` 中的
// BIG_HERO_SHADER_BINDINGS_GLSL 宏逐一对应；改动任何一处都要同步另一处。
// 目的：让 C++ 描述符布局与 GLSL layout 限定符共享同一份“数字来源”。
// ============================================================================
namespace BigHero::Render::ShaderBindings
{
// ---- 描述符集索引 ----
inline constexpr uint32_t kSetCamera = 0;      // 前向/蒙皮：相机 UBO
inline constexpr uint32_t kSetPost = 0;        // 后处理 / IBL / SSAO / SSR：本 Pass 输入集合
inline constexpr uint32_t kSetMaterial = 1;    // 材质与光照
inline constexpr uint32_t kSetGBuffer = 2;     // 延迟光照：GBuffer 输入
inline constexpr uint32_t kSetPointShadow = 2; // 立方体阴影：点光阴影 UBO
inline constexpr uint32_t kSetAO = 3;          // 延迟光照：SSAO 输出
inline constexpr uint32_t kSetSkinning = 3;    // 蒙皮：骨骼矩阵 UBO

// ---- set CAMERA ----
inline constexpr uint32_t kCameraUBO = 0;

// ---- set MATERIAL（0..9，与所有消费该集合的着色器一致）----
inline constexpr uint32_t kMaterialLightUBO = 0;
inline constexpr uint32_t kMaterialAlbedoTex = 1;
inline constexpr uint32_t kMaterialNormalTex = 2;
inline constexpr uint32_t kMaterialShadowMap = 3;
inline constexpr uint32_t kMaterialEnvMap = 4;
inline constexpr uint32_t kMaterialIrradianceMap = 5;
inline constexpr uint32_t kMaterialPrefilteredMap = 6;
inline constexpr uint32_t kMaterialBrdfLut = 7;
inline constexpr uint32_t kMaterialPointShadowMap = 8;
inline constexpr uint32_t kMaterialObjectTex = 9;
// 逐物体纹理池槽数（set1 binding9 数组长度，须与着色器 uObjectTex 数组一致）
inline constexpr uint32_t kMaterialObjectTextureSlots = 16;

// ---- set GBUFFER ----
inline constexpr uint32_t kGBufferAlbedo = 0;
inline constexpr uint32_t kGBufferNormal = 1;
inline constexpr uint32_t kGBufferPosition = 2;

// ---- set AO ----
inline constexpr uint32_t kAOTex = 0;

// ---- set SKINNING ----
inline constexpr uint32_t kSkinningUBO = 0;

// ---- set POINT_SHADOW ----
inline constexpr uint32_t kPointShadowUBO = 0;

// 与 GLSL 侧一致性锚点：纹理池 16 槽、材质集合共 10 个绑定（0..9）。
static_assert(kMaterialObjectTextureSlots == 16, "uObjectTex array length must stay in sync with GLSL [16]");
static_assert(kMaterialObjectTex + 1 == 10, "material set must expose bindings 0..9");
} // namespace BigHero::Render::ShaderBindings
