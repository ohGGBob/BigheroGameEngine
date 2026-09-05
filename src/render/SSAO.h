#pragma once
// 屏幕空间环境光遮蔽（SSAO）：
// 从 GBuffer 采样世界坐标与法线，用半球核计算 AO，经模糊后供延迟光照 Pass 采样。
// 仅在延迟渲染模式下使用（需要 GBuffer 位置/法线纹理）。
#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;
class Image;
namespace Render
{
class GraphicsPipeline;
struct ShaderModuleHandle;

class SSAO
{
  public:
    SSAO() = default;
    ~SSAO();

    SSAO(const SSAO&) = delete;
    SSAO& operator=(const SSAO&) = delete;
    SSAO(SSAO&&) noexcept;
    SSAO& operator=(SSAO&&) noexcept;

    // 初始化：创建半分辨率 AO 缓冲、渲染通道、管线、描述符
    void Init(const Context& ctx, VkExtent2D extent);
    // 销毁全部 Vulkan 资源
    void Destroy() noexcept;
    // 窗口尺寸变化时重建图像与帧缓冲
    void Recreate(const Context& ctx, VkExtent2D extent);

    // 录制 SSAO + 模糊 Pass（在几何 Pass 之后、光照 Pass 之前调用）
    // positionView / normalView：GBuffer 位置/法线图像视图
    // viewProj / cameraPos：当前帧相机参数
    void RecordPass(VkCommandBuffer cmd, VkImageView positionView, VkImageView normalView, const glm::mat4& viewProj,
                    const glm::vec3& cameraPos);

    [[nodiscard]] VkImageView GetAOView() const noexcept;
    // 渲染图：AO 最终输出图像（垂直模糊后，供光照 Pass 采样）
    [[nodiscard]] VkImage GetAOImage() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept { return aoImage_ != nullptr; }

    // 可调参数
    float radius = 0.5f;   // 采样半径（世界单位）
    float bias = 0.025f;   // 深度偏差
    float strength = 1.5f; // AO 强度（幂次）

  private:
    void CreateImages(const Context& ctx);
    void CreateRenderPasses(const Context& ctx);
    void CreateFramebuffers();
    void CreatePipelines(const Context& ctx);
    void CreateDescriptorResources(const Context& ctx);
    void UpdateDescriptorSets(VkImageView positionView, VkImageView normalView);
    void DestroyFramebuffers() noexcept;
    void DestroyPipelines() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkExtent2D halfExtent_{};

    // 半分辨率 AO 缓冲（R8）
    std::unique_ptr<Image> aoImage_;
    std::unique_ptr<Image> aoBlurImage_;

    // 渲染通道
    VkRenderPass ssaoRenderPass_ = VK_NULL_HANDLE;
    VkRenderPass blurRenderPass_ = VK_NULL_HANDLE;

    // 帧缓冲
    VkFramebuffer aoFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer aoBlurFramebuffer_ = VK_NULL_HANDLE;

    // 管线
    std::unique_ptr<GraphicsPipeline> ssaoPipeline_;
    std::unique_ptr<GraphicsPipeline> blurPipeline_;

    // 描述符
    VkDescriptorSetLayout ssaoLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout blurLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet ssaoSet_ = VK_NULL_HANDLE;
    VkDescriptorSet blurSet_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
};
} // namespace Render
} // namespace BigHero
