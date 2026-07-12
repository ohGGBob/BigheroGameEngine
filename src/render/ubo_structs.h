#pragma once
#include <glm/glm.hpp>
#include <cstddef>

namespace BigHero::Render
{
    /**
     * @brief 相机UBO，着色器 set=0 binding=0，std140 布局
     * mat4 占64字节，天然16字节对齐，无需额外填充
     */
    struct CameraUBO
    {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;

        /// 缓冲总字节大小，创建Vulkan缓冲直接使用
        static constexpr size_t ByteSize = sizeof(CameraUBO);

        /// 默认单位矩阵初始化，防止脏数据传入GPU
        CameraUBO() : model(1.0f), view(1.0f), proj(1.0f) {}

        // 编译期校验布局尺寸，不匹配直接编译报错
        static_assert(ByteSize == 192, "CameraUBO std140 size mismatch! Check matrix layout.");
    };

    /**
     * @brief 方向光UBO，着色器 set=1 binding=0，std140 布局
     * std140：vec3 占用12字节，每个vec3所在块必须16字节对齐，紧跟1个float填充
     * 每4个float自动组成16字节对齐块
     */
    struct LightUBO
    {
        glm::vec3 lightDir;
        float padLightDir;

        glm::vec3 lightColor;
        float padLightColor;

        glm::vec3 cameraPos;
        float ambientFactor;

        float specPower;
        float specStrength;
        float padSpecA;
        float padSpecB;

        static constexpr size_t ByteSize = sizeof(LightUBO);

        LightUBO()
            : lightDir(0.f), padLightDir(0.f),
              lightColor(1.f), padLightColor(0.f),
              cameraPos(0.f), ambientFactor(0.1f),
              specPower(32.f), specStrength(1.f), padSpecA(0.f), padSpecB(0.f)
        {}

        // 修正正确字节尺寸：4组vec4(16*4=64)，原断言数值正确，保留
        static_assert(ByteSize == 64, "LightUBO std140 size mismatch! Check vec3 padding.");
    };
}