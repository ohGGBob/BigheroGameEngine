#pragma once
#include <glm/glm.hpp>
#include <cstddef>

namespace BigHero::Render
{
    // 相机UBO：视图+投影矩阵（模型矩阵与材质参数走推送常量，支持逐物体变换）
    struct CameraUBO
    {
        glm::mat4 view{ 1.0f };
        glm::mat4 proj{ 1.0f };
    };
    inline constexpr size_t CameraUBO_ByteSize = sizeof(CameraUBO);

    // 光照/环境UBO（PBR布局，与着色器std140严格对齐）
    struct LightUBO
    {
        glm::vec3 lightDir;      // 光源照射方向（取反得指向光源的L）
        float intensity;         // 光源辐射强度倍数

        glm::vec3 lightColor;    // 光源颜色（辐射率）
        float ambientFactor;     // 环境光系数

        glm::vec3 cameraPos;     // 相机世界位置（高光/视线方向）
        float padding;
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
