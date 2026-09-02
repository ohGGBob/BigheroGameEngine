#pragma once
// 后处理处理器：Bloom（亮部提取 → 高斯模糊 → 合成）+ ACES 色调映射。
//
// 架构：
// - 场景由 Renderer 渲染到离屏缓冲（PostProcessor 提供颜色/解析图，Renderer 提供深度图）
// - PostProcessor 管理后处理中间缓冲、渲染通道、管线、描述符集
// - RecordBloom() 录制完整后处理链并输出到交换链
// - 复用现有场景渲染通道，避免重建场景管线

#include "render/Image.h"
#include "render/pipeline.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;
}

namespace BigHero::Render
{
class PostProcessor
{
  public:
    PostProcessor() = default;
    ~PostProcessor() { Destroy(); }

    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;

    void Init(const Context& ctx, VkExtent2D extent, VkFormat colorFormat, VkSampleCountFlagBits samples,
              const std::vector<VkImageView>& swapchainViews);

    void Destroy();

    // 窗口尺寸变化时重建所有尺寸相关资源
    void Recreate(const Context& ctx, VkExtent2D extent, const std::vector<VkImageView>& swapchainViews);

    // 录制完整 Bloom 后处理链：亮部提取 → 水平模糊 → 垂直模糊 → 合成到交换链
    void RecordBloom(VkCommandBuffer cmd, uint32_t swapchainIndex, VkExtent2D extent);

    [[nodiscard]] bool IsValid() const noexcept { return initialized_; }

    // 离屏缓冲视图（供 Renderer 创建离屏帧缓冲）
    [[nodiscard]] VkImageView OffscreenMsaaColorView() const noexcept { return offscreenMsaaColor_.View(); }
    [[nodiscard]] VkImageView OffscreenResolveView() const noexcept { return offscreenResolve_.View(); }
    [[nodiscard]] bool UseMsaa() const noexcept { return samples_ != VK_SAMPLE_COUNT_1_BIT; }

    // 可调参数（编辑器实时修改）
    float bloomThreshold = 0.8f;
    float bloomSoftKnee = 0.5f;
    float bloomStrength = 0.6f;
    float exposure = 1.0f;

    // 升级 21：色调分级（Color Grading）参数，作用于合成阶段（ACES 之后）。
    // 默认值均为"无操作"，编辑器不改时画面不变。映射见 render/ColorGrading.h。
    float gradeSaturation = 1.0f; // 饱和度：1=不变，0=全灰，>1 更艳
    float gradeContrast = 1.0f;   // 对比度：以中灰(0.5)为中心，1=不变
    float gradeLift = 0.0f;       // 加性偏移（暗部提升），三通道统一
    float gradeGain = 1.0f;       // 乘性缩放（整体亮度），1=不变
    float gradeGamma = 1.0f;      // 幂次（<1 提亮中间调，>1 压暗），1=不变

  private:
    void CreateImages(const Context& ctx);
    void CreateFramebuffers(const std::vector<VkImageView>& swapchainViews);
    void CreateRenderPasses();
    void CreatePipelines(const Context& ctx);
    void CreateDescriptorResources(const Context& ctx);
    void UpdateDescriptorSets();
    void DestroyFramebuffers();
    void DestroyPipelines();

    VkDevice device_ = VK_NULL_HANDLE;
    bool initialized_ = false;

    VkExtent2D extent_{0, 0};
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;

    // 离屏场景渲染：MSAA 颜色 + 解析（可采样）。深度图由 Renderer 提供。
    Image offscreenMsaaColor_;
    Image offscreenResolve_;

    // 后处理中间缓冲（半分辨率）
    VkExtent2D halfExtent_{0, 0};
    Image brightImage_;
    Image blurImageA_;
    Image blurImageB_;

    // 后处理渲染通道（单颜色附件，最终布局 SHADER_READ_ONLY）
    VkRenderPass postRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer brightFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer blurAFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer blurBFramebuffer_ = VK_NULL_HANDLE;

    // 输出渲染通道（写入交换链，最终布局 COLOR_ATTACHMENT_OPTIMAL）
    VkRenderPass outputRenderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> outputFramebuffers_;

    // 全屏管线
    std::unique_ptr<GraphicsPipeline> brightPipeline_;
    std::unique_ptr<GraphicsPipeline> blurPipeline_;
    std::unique_ptr<GraphicsPipeline> compositePipeline_;

    // 描述符资源
    VkDescriptorSetLayout descSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet brightDescSet_ = VK_NULL_HANDLE;
    VkDescriptorSet blurHDescSet_ = VK_NULL_HANDLE;
    VkDescriptorSet blurVDescSet_ = VK_NULL_HANDLE;
    VkDescriptorSet compositeDescSet_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
};
} // namespace BigHero::Render
