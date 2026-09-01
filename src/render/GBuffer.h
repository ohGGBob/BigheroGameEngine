#pragma once
// 延迟渲染 GBuffer 格式与布局定义（纯常量，可离线单测，不依赖 Vulkan 运行）。
// 几何子通道把以下信息写入多渲染目标（MRT），延迟光照子通道以输入附件读回。
#include <vulkan/vulkan.h>
#include <cstdint>

namespace BigHero::Render
{
    // GBuffer 三张颜色附件的格式选择：
    //  - 反照率/金属度：RGBA8（反照率 UNORM 足够，金属度存 alpha）
    //  - 法线/粗糙度：RGBA16F（法线需高精度避免带状瑕疵，粗糙度存 alpha）
    //  - 世界坐标：RGBA16F（延迟光照重建需要精确世界位置；alpha 作几何标记）
    struct GBufferFormats
    {
        VkFormat albedo = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat normal = VK_FORMAT_R16G16B16A16_SFLOAT;
        VkFormat position = VK_FORMAT_R16G16B16A16_SFLOAT;
    };

    [[nodiscard]] inline constexpr GBufferFormats DefaultGBufferFormats() noexcept
    {
        return GBufferFormats{};
    }

    // MRT 输出位置（几何子通道片段着色器的 layout(location=...)）
    inline constexpr uint32_t kGBufferAlbedoLocation = 0;
    inline constexpr uint32_t kGBufferNormalLocation = 1;
    inline constexpr uint32_t kGBufferPositionLocation = 2;

    // 输入附件数量（延迟光照子通道读取的 GBuffer 张数）
    inline constexpr uint32_t kGBufferInputAttachmentCount = 3;

    // 几何标记：GBuffer position 附件的 alpha 通道。
    // 几何像素写 1.0；背景像素由渲染通道清零，光照阶段据此走天空分支。
    inline constexpr float kGBufferGeometryMask = 1.0f;

    // 三张颜色附件在延迟渲染通道中的附件下标（须与着色器 input_attachment_index 对应）
    inline constexpr uint32_t kGBufferAlbedoAttachment = 0;
    inline constexpr uint32_t kGBufferNormalAttachment = 1;
    inline constexpr uint32_t kGBufferPositionAttachment = 2;
    inline constexpr uint32_t kGBufferDepthAttachment = 3;
    inline constexpr uint32_t kGBufferSwapchainAttachment = 4;
}
