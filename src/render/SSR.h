#pragma once
// 屏幕空间反射（SSR）：
// 从 GBuffer 采样世界坐标与法线，计算反射射线并在屏幕空间 ray march，
// 命中时采样场景颜色，经模糊后供合成 Pass 与场景颜色混合。
// 仅在延迟渲染模式下使用（需要 GBuffer 位置/法线 + 离屏场景颜色）。
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

class SSR
{
  public:
    SSR() = default;
    ~SSR();

    SSR(const SSR&) = delete;
    SSR& operator=(const SSR&) = delete;
    SSR(SSR&&) noexcept;
    SSR& operator=(SSR&&) noexcept;

    // 初始化：创建半分辨率反射缓冲、渲染通道、管线、描述符
    void Init(const Context& ctx, VkExtent2D extent);
    // 销毁全部 Vulkan 资源
    void Destroy() noexcept;
    // 窗口尺寸变化时重建图像与帧缓冲
    void Recreate(const Context& ctx, VkExtent2D extent);

    // 录制 SSR ray march + 模糊 Pass（在光照 Pass 之后、合成 Pass 之前调用）
    // positionView / normalView：GBuffer 位置/法线图像视图
    // sceneColorView：离屏场景颜色（光照 Pass 输出）
    // viewProj / cameraPos：当前帧相机参数
    void RecordPass(VkCommandBuffer cmd, VkImageView positionView, VkImageView normalView, VkImageView sceneColorView,
                    const glm::mat4& viewProj, const glm::vec3& cameraPos);

    [[nodiscard]] VkImageView GetReflectionView() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept { return reflectionImage_ != nullptr; }

    // 可调参数
    float maxDistance = 20.0f; // 射线最大距离（世界单位）
    int stepCount = 32;        // ray march 步数
    float thickness = 0.1f;    // 厚度容差（世界单位）
    float edgeFade = 0.1f;     // 屏幕边缘淡出宽度（UV 空间）

  private:
    void CreateImages(const Context& ctx);
    void CreateRenderPasses(const Context& ctx);
    void CreateFramebuffers();
    void CreatePipelines(const Context& ctx);
    void CreateDescriptorResources(const Context& ctx);
    void UpdateDescriptorSets(VkImageView positionView, VkImageView normalView, VkImageView sceneColorView);
    void DestroyFramebuffers() noexcept;
    void DestroyPipelines() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkExtent2D halfExtent_{};

    // 半分辨率反射缓冲（RGBA16F：rgb=反射颜色, a=命中强度）
    std::unique_ptr<Image> reflectionImage_;
    std::unique_ptr<Image> reflectionBlurImage_;

    // 渲染通道
    VkRenderPass rayRenderPass_ = VK_NULL_HANDLE;
    VkRenderPass blurRenderPass_ = VK_NULL_HANDLE;

    // 帧缓冲
    VkFramebuffer rayFramebuffer_ = VK_NULL_HANDLE;
    VkFramebuffer blurFramebuffer_ = VK_NULL_HANDLE;

    // 管线
    std::unique_ptr<GraphicsPipeline> rayPipeline_;
    std::unique_ptr<GraphicsPipeline> blurPipeline_;

    // 描述符
    VkDescriptorSetLayout rayLayout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout blurLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet raySet_ = VK_NULL_HANDLE;
    VkDescriptorSet blurSet_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
};
} // namespace Render
} // namespace BigHero
