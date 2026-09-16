#ifndef BIG_HERO_SHADER_BINDINGS_GLSL
#define BIG_HERO_SHADER_BINDINGS_GLSL

// ============================================================================
// 中央描述符集/绑定常量（阶段一 · 1.3）
//
// 目的：把散落在 33 个着色器里的 `layout(set = N, binding = M)` 魔法数字
//      收敛为具名常量，消除硬编码、便于全局检索与重排。
//
// 契约：本文件的数值 MUST 与 C++ 侧 `src/render/shader_bindings.h`
//      （namespace BigHero::Render::ShaderBindings）逐一对应。
//      改动任何一处都必须同步另一处。
//
// 说明：set 索引在不同管线间按“绑定契约”复用（例如 set2 既是延迟光照的
//      GBuffer 输入、也是点光阴影 UBO），因此下面按用途给出同值异名的常量，
//      具体语境由各着色器注释说明。
// ============================================================================

// ---- 描述符集索引 ----
#define BH_SET_CAMERA       0 // 前向/蒙皮：相机 UBO
#define BH_SET_POST         0 // 后处理 / IBL / SSAO / SSR：本 Pass 的输入集合
#define BH_SET_MATERIAL     1 // 材质与光照（前向 / 延迟 / GBuffer / 天空盒一致）
#define BH_SET_GBUFFER      2 // 延迟光照：GBuffer 输入（gAlbedo/gNormal/gPosition）
#define BH_SET_POINT_SHADOW 2 // 立方体阴影：点光阴影 UBO
#define BH_SET_AO           3 // 延迟光照：SSAO 输出
#define BH_SET_SKINNING     3 // 蒙皮：骨骼矩阵 UBO

// ---- set CAMERA ----
#define BH_CAMERA_UBO 0

// ---- set MATERIAL（binding 0..9，全局一致）----
#define BH_MATERIAL_LIGHT_UBO        0 // 光照参数 UBO（std140）
#define BH_MATERIAL_ALBEDO_TEX       1 // 反照率贴图
#define BH_MATERIAL_NORMAL_TEX       2 // 法线贴图
#define BH_MATERIAL_SHADOW_MAP       3 // CSM 阴影图集
#define BH_MATERIAL_ENV_MAP          4 // 环境立方图
#define BH_MATERIAL_IRRADIANCE_MAP   5 // 漫反射 IBL 立方图
#define BH_MATERIAL_PREFILTERED_MAP  6 // 镜面 IBL 预过滤立方图
#define BH_MATERIAL_BRDF_LUT         7 // BRDF LUT
#define BH_MATERIAL_POINT_SHADOW_MAP 8 // 点光源立方阴影图
#define BH_MATERIAL_OBJECT_TEX       9 // 逐物体纹理池（16 槽数组）

// ---- set GBUFFER ----
#define BH_GBUFFER_ALBEDO   0
#define BH_GBUFFER_NORMAL   1
#define BH_GBUFFER_POSITION 2

// ---- set AO ----
#define BH_AO_TEX 0

// ---- set SKINNING ----
#define BH_SKINNING_UBO 0

// ---- set POINT_SHADOW ----
#define BH_POINT_SHADOW_UBO 0

// ---- 后处理 / IBL / SSAO / SSR 的 set-POST 输入槽位 ----
// 这些 Pass 的 set0 语义随 Pass 而异（漫反射/法线/深度/雾等），
// 故按槽位序号统一命名，具体含义见各着色器内的局部注释。
#define BH_PP_SLOT0 0
#define BH_PP_SLOT1 1
#define BH_PP_SLOT2 2
#define BH_PP_SLOT3 3
#define BH_PP_SLOT4 4
#define BH_PP_SLOT5 5

#endif // BIG_HERO_SHADER_BINDINGS_GLSL
