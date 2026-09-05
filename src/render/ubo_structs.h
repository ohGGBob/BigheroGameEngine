#pragma once
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

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

    glm::mat4 lightSpaceMatrix; // 方向光视空间（阴影投影）

    GpuPointLight lights[kMaxPointLights];
};
inline constexpr size_t LightUBO_ByteSize = sizeof(LightUBO);

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

template<> constexpr size_t GetUboByteSize<PointShadowUBO>()
{
    return PointShadowUBO_ByteSize;
}

template<> constexpr size_t GetUboByteSize<SkinningUBO>()
{
    return SkinningUBO_ByteSize;
}
} // namespace BigHero::Render
