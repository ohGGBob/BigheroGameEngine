#pragma once
#include "render/Image.h"
#include <cstdint>
#include <functional>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;

// 方向光阴影贴图：固定尺寸深度预通道资源（图像/渲染通道/帧缓冲/采样器）
// 阴影图尺寸固定、不依赖交换链，创建一次即可，无需随窗口重建
class ShadowMap
{
  public:
    ShadowMap() = default;
    ~ShadowMap() { Destroy(); }

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    void Create(const Context& ctx, uint32_t size = 2048);
    void Destroy();

    // 录制深度预通道：清空深度后回调外部绘制场景几何
    void RecordPass(VkCommandBuffer cmd, const std::function<void(VkCommandBuffer)>& drawScene) const;

    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return renderPass_; }
    [[nodiscard]] VkImageView View() const noexcept { return depthImage_.View(); }
    [[nodiscard]] VkSampler Sampler() const noexcept { return sampler_; }
    [[nodiscard]] uint32_t Size() const noexcept { return size_; }

  private:
    const Context* ctx_ = nullptr;
    Image depthImage_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    uint32_t size_ = 0;
};
} // namespace BigHero
