#pragma once
// 延迟渲染 GBuffer 格式与布局定义（纯常量，可离线单测，不依赖 Vulkan 运行）。
// 几何子通道把以下信息写入多渲染目标（MRT），延迟光照子通道以输入附件读回。
#include <cstdint>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
// GBuffer 四张颜色附件的格式选择：
//  - 反照率/金属度：RGBA8（反照率 UNORM 足够，金属度存 alpha）
//  - 法线/粗糙度：RGBA16F（法线需高精度避免带状瑕疵，粗糙度存 alpha）
//  - 世界坐标：RGBA16F（延迟光照重建需要精确世界位置；alpha 作几何标记）
//  - 光照贴图辐射度（0.22.37）：RGBA16F（静态批次烘焙出射辐射度需 HDR；a 作静态标记）
struct GBufferFormats
{
    VkFormat albedo = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat normal = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat position = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat lm = VK_FORMAT_R16G16B16A16_SFLOAT;
};

[[nodiscard]] inline constexpr GBufferFormats DefaultGBufferFormats() noexcept
{
    return GBufferFormats{};
}

// MRT 输出位置（几何子通道片段着色器的 layout(location=...)）
inline constexpr uint32_t kGBufferAlbedoLocation = 0;
inline constexpr uint32_t kGBufferNormalLocation = 1;
inline constexpr uint32_t kGBufferPositionLocation = 2;
inline constexpr uint32_t kGBufferLmLocation = 3;

// 输入附件数量（延迟光照子通道读取的 GBuffer 张数）
inline constexpr uint32_t kGBufferInputAttachmentCount = 4;

// 几何标记：GBuffer position 附件的 alpha 通道。
// 几何像素写 1.0；背景像素由渲染通道清零，光照阶段据此走天空分支。
inline constexpr float kGBufferGeometryMask = 1.0f;

// 静态批次标记：GBuffer 光照贴图附件的 alpha 通道。
// 静态批次像素写 1.0（rgb=烘焙辐射度）；其余像素由渲染通道清零，
// 光照阶段据此跳过实时 PBR 直接输出烘焙辐射度。
inline constexpr float kGBufferStaticMask = 1.0f;

// 四张颜色附件 + 深度在延迟渲染通道中的附件下标（须与着色器 input_attachment_index 对应）
inline constexpr uint32_t kGBufferAlbedoAttachment = 0;
inline constexpr uint32_t kGBufferNormalAttachment = 1;
inline constexpr uint32_t kGBufferPositionAttachment = 2;
inline constexpr uint32_t kGBufferLmAttachment = 3;
inline constexpr uint32_t kGBufferDepthAttachment = 4;
inline constexpr uint32_t kGBufferSwapchainAttachment = 5;
} // namespace BigHero::Render
