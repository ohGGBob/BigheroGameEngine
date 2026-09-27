#pragma once
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

#include "shader_bindings.h"

namespace BigHero::Render
{
// 相机UBO：视图+投影矩阵（模型矩阵与材质参数走推送常量，支持逐物体变换）
struct CameraUBO
{
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
};
inline constexpr size_t CameraUBO_ByteSize = sizeof(CameraUBO);

// 点光源（GPU布局，std140下48字节）。
// 注意：std140 规则规定"结构体数组"的数组步长 = 结构体大小向上取整到 16 的倍数，
// 因此元素大小必须取 16 的倍数（48），否则 CPU(C++) 数组步长与 GPU 不一致。
struct GpuPointLight
{
    glm::vec3 position;
    float intensity;
    glm::vec3 color;
    float radius;
    float castsShadow; // 1.0=投射立方体阴影
    float pad[3];      // 补齐到 48 字节（16 的倍数，匹配 std140 数组步长）
};
static_assert(sizeof(GpuPointLight) == 48, "GpuPointLight必须为16的倍数以匹配std140数组步长");

// 点光源槽位数（着色器UBO定长数组）
inline constexpr uint32_t kMaxPointLights = 8;

// 方向光级联阴影（CSM）级联数：2x2 深度图集，级联 c 占据 (c&1, c/2) 子块
inline constexpr uint32_t kMaxCascades = 4;

// 光照/环境UBO（PBR多光源布局，与着色器std140严格对齐）
struct LightUBO
{
    glm::vec3 lightDir; // 方向光照射方向（取反得指向光源的L）
    float dirIntensity; // 方向光辐射强度倍数

    glm::vec3 lightColor; // 方向光颜色（辐射率）
    float ambientFactor;  // 环境光系数

    glm::vec3 cameraPos;   // 相机世界位置（高光/视线方向）
    float pointLightCount; // 激活的点光源数量

    float shadowStrength; // 阴影浓度（0关闭~1全影）
    float shadowBias;     // 深度比较偏移
    float iblStrength;    // IBL环境光照强度（0=常数环境光，1=完整IBL）
    float exposure;       // 色调映射曝光（HDR->LDR 前对辐射率的整体缩放）

    // ---- 级联阴影（CSM）----
    // 每级联一个正交光视投影矩阵；级联 c 的子块偏移 = (c&1, c/2) * 0.5（2x2 图集）
    glm::mat4 lightSpaceMatrices[kMaxCascades];
    // 轴向视图深度分割边界：级联 c 覆盖深度带 ((c>0? splits[c-1] : 0), splits[c]]
    glm::vec4 cascadeSplits;
    // xyz = 相机前向单位向量（轴向深度度量 dot(fwd, p-camPos)），w = 阴影最远绘制距离
    glm::vec4 cameraForward;
    // 天空盒调色：rgb = 颜色乘数，w = 强度（时段/氛围预设用，默认 (1,1,1,1) 原样）
    glm::vec4 skyTint{1.0f, 1.0f, 1.0f, 1.0f};

    GpuPointLight lights[kMaxPointLights];

    // ---- LightProbe（SH9）相机位置单探针辐照度注入 ----
    // 由 Application::UpdateUniforms 每帧按相机世界位置采样探针体写入；未烘焙时恒为 0（渲染零变化）。
    // 契约：值即「可直接乘 albedo 的辐照度颜色」（LightProbe.h 约定 Evaluate 输出）。
    // std140：vec3 按 16 字节对齐，其后 float 正好填满余 4 字节，凑成一个 16 字节槽。
    glm::vec3 probeAmbient{0.0f}; // 探针注入的环境光辐照度
    float probePadding{0.0f};     // std140 对齐填充
};
inline constexpr size_t LightUBO_ByteSize = sizeof(LightUBO);
// std140 UBO 块大小必须是 16 的倍数；在原 752 字节基础上追加 probeAmbient(vec3)+probePadding(float)=16 字节。
static_assert(sizeof(LightUBO) % 16 == 0, "LightUBO 大小必须是 16 的倍数（std140 UBO 块规则）");
static_assert(sizeof(LightUBO) == 768, "LightUBO std140 布局大小校验：probeAmbient 追加后应为 768 字节");

// 延迟光照逐片元探针辐照度体 UBO（std140，独立于 LightUBO，不影响前向/后处理等其他 pass）。
// 动机：延迟光照 pass 是全屏 quad，拿不到逐实例属性；改为按片元世界位置在探针网格上三线性插值。
// 数据约定：CPU 端对每探针以世界 up 法线预求值 SH 得漫反射辐照度 RGB（与前向逐实例路径
//   SampleIrradiance(pos, up) 同口径），片元着色器仅做 8 邻域三线性插值，不再在 GPU 求值 SH。
// 布局：3 个 vec4 头（网格参数）+ probes[槽位]（.rgb=up-求值辐照度，.a=有效性 1/0）。
// 未烘焙（dimsCount.w==0）时片元跳过采样，回退 lightUbo.probeAmbient（相机位置单探针，零变化）。
struct ProbeUBO
{
    glm::ivec4 dimsCount;  // xyz=网格维度, w=探针总数（线性索引数；0=未烘焙）
    glm::vec4 originPad;   // xyz=探针网格原点, w=未用
    glm::vec4 spacingPad;  // xyz=探针网格间距, w=未用
    glm::vec4 probes[ShaderBindings::kMaterialProbeMax]; // .rgb=up-求值辐照度, .a=有效性
};
inline constexpr size_t ProbeUBO_ByteSize = sizeof(ProbeUBO);
// std140：3 个 vec4 头(48) + 槽位 vec4 数组(16*kMaterialProbeMax)，天然 16 对齐。
static_assert(sizeof(ProbeUBO) % 16 == 0, "ProbeUBO 大小必须是 16 的倍数（std140 UBO 块规则）");
static_assert(sizeof(ProbeUBO) == 48 + 16 * ShaderBindings::kMaterialProbeMax,
              "ProbeUBO std140 布局大小校验：3 个 vec4 头 + 槽位数组");

// 反射探针（U2-L2 接线 v1）：局部环境烘焙镜面的 GPU 侧数据（std140，set1 binding11）。
// 每探针 12 vec4：位置+有效、影响盒 min/max、9 个未预滤波 SH 系数（rgb）。
// 粗糙度带通衰减 f_l = exp(-l(l+1)·α²/2) 在片元着色器施加（与 CPU PrefilterForRoughness 同款）。
struct GpuReflectProbe
{
    glm::vec4 posValid;  // xyz=探针位置, w=1有效 / 0无效
    glm::vec4 boxMin;    // xyz=影响盒 min, w=未用
    glm::vec4 boxMax;    // xyz=影响盒 max, w=未用
    glm::vec4 sh[9];     // rgb=SH 系数（原始辐射亮度；consume Sh9::c[k] 顺序一致）, a=未用
};
inline constexpr size_t GpuReflectProbe_ByteSize = sizeof(GpuReflectProbe);
static_assert(sizeof(GpuReflectProbe) == 12 * 16, "每反射探针 12 vec4（std140）");

struct ReflectProbeUBO
{
    glm::vec4 countPad;                                            // x=有效探针数（0=未烘焙，采样跳过）
    GpuReflectProbe probes[ShaderBindings::kMaterialReflectProbeMax];
};
inline constexpr size_t ReflectProbeUBO_ByteSize = sizeof(ReflectProbeUBO);
static_assert(sizeof(ReflectProbeUBO) % 16 == 0, "ReflectProbeUBO 大小必须是 16 的倍数（std140）");
static_assert(sizeof(ReflectProbeUBO) == 16 + GpuReflectProbe_ByteSize * ShaderBindings::kMaterialReflectProbeMax,
              "ReflectProbeUBO std140 布局大小校验：1 个 vec4 头 + 槽位数组");

// 点光源阴影：立方体阴影贴图所需的 6 个面视投影矩阵（std140 布局）
// 每矩阵 64 字节（mat4 按 16 字节对齐），数组连续紧密排布
struct PointShadowUBO
{
    glm::mat4 faceMatrices[6]; // 顺序：+X,-X,+Y,-Y,+Z,-Z
};
inline constexpr size_t PointShadowUBO_ByteSize = sizeof(PointShadowUBO);
static_assert(sizeof(PointShadowUBO) == 6 * sizeof(glm::mat4), "PointShadowUBO 必须为 6 个 mat4 的紧密数组");

// ---- GPU 蒙皮：骨骼矩阵调色板 ----
// 顶点着色器按逐顶点关节索引采样调色板完成蒙皮，替代 CPU 蒙皮以支持大规模角色。
// std140 下 mat4 数组的数组步长 = 64 字节，与 C++ 端 mat4 数组（紧密排布）一致，
// 故 CPU 可直接 memcpy 整个调色板到 UBO，无需逐元素重排。
inline constexpr uint32_t kMaxSkinBones = 128; // 单次绘制最大骨骼数

struct SkinningUBO
{
    glm::mat4 boneMatrices[kMaxSkinBones]; // 皮肤矩阵 = 全局关节矩阵 * 逆绑定矩阵
};
inline constexpr size_t SkinningUBO_ByteSize = sizeof(SkinningUBO);
static_assert(sizeof(SkinningUBO) == kMaxSkinBones * sizeof(glm::mat4),
              "SkinningUBO 必须为 kMaxSkinBones 个 mat4 的紧密数组（std140 步长 64 字节）");

template<typename T> constexpr size_t GetUboByteSize();

template<> constexpr size_t GetUboByteSize<CameraUBO>()
{
    return CameraUBO_ByteSize;
}

template<> constexpr size_t GetUboByteSize<LightUBO>()
{
    return LightUBO_ByteSize;
}

template<> constexpr size_t GetUboByteSize<ProbeUBO>()
{
    return ProbeUBO_ByteSize;
}

template<> constexpr size_t GetUboByteSize<ReflectProbeUBO>()
{
    return ReflectProbeUBO_ByteSize;
}

template<> constexpr size_t GetUboByteSize<PointShadowUBO>()
{
    return PointShadowUBO_ByteSize;
}

template<> constexpr size_t GetUboByteSize<SkinningUBO>()
{
    return SkinningUBO_ByteSize;
}
} // namespace BigHero::Render
