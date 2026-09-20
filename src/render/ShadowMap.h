#pragma once
#include "render/Image.h"
#include <cstdint>
#include <functional>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;

// 方向光级联阴影贴图（CSM）：单张深度图集 2x2 平铺 kCascadeCount 个级联子块，
// 尺寸固定、不依赖交换链，创建一次即可，无需随窗口重建。
// RecordPass 单渲染通道内逐级联切换 viewport/scissor，回调按级联推送对应光视矩阵。
class ShadowMap
{
  public:
    static constexpr uint32_t kCascadeCount = 4; // 与 Render::kMaxCascades 一致（2x2 图集）

    ShadowMap() = default;
    ~ShadowMap() { Destroy(); }

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    void Create(const Context& ctx, uint32_t size = 2048);
    void Destroy();

    // 录制级联深度预通道：清空图集深度后，逐级联设定子块 viewport/scissor 并回调绘制场景几何
    void RecordPass(VkCommandBuffer cmd, const std::function<void(VkCommandBuffer, uint32_t cascade)>& drawScene) const;

    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return renderPass_; }
    [[nodiscard]] VkImageView View() const noexcept { return depthImage_.View(); }
    [[nodiscard]] VkSampler Sampler() const noexcept { return sampler_; }
    [[nodiscard]] uint32_t Size() const noexcept { return size_; }
    // 单个级联子块的边长（像素）
    [[nodiscard]] uint32_t CascadeTileSize() const noexcept { return size_ / kCascadeCount; }

  private:
    const Context* ctx_ = nullptr;
    Image depthImage_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    uint32_t size_ = 0;
};
} // namespace BigHero
