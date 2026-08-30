#pragma once
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>

namespace BigHero::Render
{
    // 相机UBO：视图+投影矩阵（模型矩阵与材质参数走推送常量，支持逐物体变换）
    struct CameraUBO
    {
        glm::mat4 view{ 1.0f };
        glm::mat4 proj{ 1.0f };
    };
    inline constexpr size_t CameraUBO_ByteSize = sizeof(CameraUBO);

    // 点光源（GPU布局，std140下32字节：位置+强度 / 颜色+半径）
    struct GpuPointLight
    {
        glm::vec3 position;
        float intensity;
        glm::vec3 color;
        float radius;
    };
    static_assert(sizeof(GpuPointLight) == 32, "GpuPointLight必须与std140布局严格对齐");

    // 点光源槽位数（着色器UBO定长数组）
    inline constexpr uint32_t kMaxPointLights = 8;

    // 光照/环境UBO（PBR多光源布局，与着色器std140严格对齐）
    struct LightUBO
    {
        glm::vec3 lightDir;        // 方向光照射方向（取反得指向光源的L）
        float dirIntensity;        // 方向光辐射强度倍数

        glm::vec3 lightColor;      // 方向光颜色（辐射率）
        float ambientFactor;       // 环境光系数

        glm::vec3 cameraPos;       // 相机世界位置（高光/视线方向）
        float pointLightCount;     // 激活的点光源数量

        float shadowStrength;      // 阴影浓度（0关闭~1全影）
        float shadowBias;          // 深度比较偏移
        float iblStrength;         // IBL环境光照强度（0=常数环境光，1=完整IBL）
        float pad1;

        glm::mat4 lightSpaceMatrix;// 方向光视空间（阴影投影）

        GpuPointLight lights[kMaxPointLights];
    };
    inline constexpr size_t LightUBO_ByteSize = sizeof(LightUBO);

    template<typename T>
    constexpr size_t GetUboByteSize();

    template<>
    constexpr size_t GetUboByteSize<CameraUBO>()
    {
        return CameraUBO_ByteSize;
    }

    template<>
    constexpr size_t GetUboByteSize<LightUBO>()
    {
        return LightUBO_ByteSize;
    }
}
