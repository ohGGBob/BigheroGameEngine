#pragma once
#include "render/Image.h"
#include <array>
#include <cstdint>
#include <functional>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;

// 点光源立方体阴影贴图：2048 立方体贴图深度附件 + 6 面独立渲染通道
// 每次渲染把立方体 6 个面各录制一个深度预通道（每面 90° 视锥），
// 主通道以 samplerCube 采样，按片段到点光源的方向查询对应面的深度。
// 尺寸固定、不依赖交换链，创建一次即可，无需随窗口重建。
class CubeShadowMap
{
  public:
    // 立方体 6 面的绑定顺序与立方体采样器 +X..-Z 一致
    static constexpr int kFaceCount = 6;

    CubeShadowMap() = default;
    ~CubeShadowMap() { Destroy(); }

    CubeShadowMap(const CubeShadowMap&) = delete;
    CubeShadowMap& operator=(const CubeShadowMap&) = delete;

    void Create(const Context& ctx, uint32_t size = 1024);
    void Destroy();

    // 录制深度预通道：清空深度后回调外部绘制场景几何（每面一次）
    void RecordPass(VkCommandBuffer cmd, const std::function<void(VkCommandBuffer, int face)>& drawScene) const;

    // 录制单个面的深度预通道（供并行录制器把 6 面分配到独立 command buffer 并行录制）
    void RecordFace(VkCommandBuffer cmd, int face,
                    const std::function<void(VkCommandBuffer, int face)>& drawScene) const;

    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return renderPass_; }
    [[nodiscard]] VkImageView View() const noexcept { return depthImage_.View(); }
    [[nodiscard]] VkSampler Sampler() const noexcept { return sampler_; }
    [[nodiscard]] uint32_t Size() const noexcept { return size_; }

  private:
    const Context* ctx_ = nullptr;
    Image depthImage_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::array<VkFramebuffer, kFaceCount> framebuffers_{};
    std::array<VkImageView, kFaceCount> viewInfos_{};
    uint32_t size_ = 0;
};
} // namespace BigHero
