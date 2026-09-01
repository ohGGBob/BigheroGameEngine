#pragma once
#include "render/GBuffer.h"
#include "render/Image.h"
#include "render/Swapchain.h"
#include "render/gpu_profiler.h"
#include "render/render_pass.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;
class Window;

// 帧渲染器：渲染通道/帧缓冲/深度附件/命令缓冲/同步对象，
// 负责每帧"等待-采集-录制-提交-呈现"循环以及窗口尺寸变化时的交换链重建
class Renderer
{
  public:
    Renderer(const Context& ctx, Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // 每帧调用：处理尺寸变化与最小化等待，随后录制并提交一帧
    // prePass(cmd, frameIndex, extent)（可选）：主渲染通道之前的深度预通道（阴影贴图等）
    // recordScene(cmd, frameIndex, extent)：由外部负责绑定管线、描述符与几何体并下达绘制命令
    // frameIndex用于选取该帧并行槽位独立的UBO/描述符
    // recordUi(cmd, imageIndex, extent)（可选）：场景通道结束后在UI覆盖层通道中录制界面
    // recordLighting(cmd, frameIndex, imageIndex, extent)（可选）：延迟渲染模式下，
    //   几何子通道之后录制全屏延迟光照绘制（采样 GBuffer 输入附件并输出到交换链）
    void DrawFrame(const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordScene,
                   const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordUi = {},
                   const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& prePass = {},
                   const std::function<void(VkCommandBuffer, uint32_t, uint32_t, VkExtent2D)>& recordLighting = {});

    // 延迟渲染开关：开启后 DrawFrame 走 GBuffer 几何子通道 + 延迟光照子通道。
    // 启用时创建 GBuffer 图像与双子通道渲染通道；关闭时释放。
    void SetDeferred(bool enabled);
    [[nodiscard]] bool IsDeferred() const noexcept { return deferredEnabled_; }

    // 延迟渲染通道与 GBuffer 视图（供外部创建管线/更新输入附件描述符集）
    [[nodiscard]] VkRenderPass GetDeferredRenderPass() const noexcept { return deferredRenderPass_; }
    [[nodiscard]] VkImageView GBufferAlbedoView(uint32_t imageIndex) const noexcept;
    [[nodiscard]] VkImageView GBufferNormalView(uint32_t imageIndex) const noexcept;
    [[nodiscard]] VkImageView GBufferPositionView(uint32_t imageIndex) const noexcept;

    // 交换链重建完成后回调（供覆盖层等依赖交换链图像的资源重建）
    void SetResizeCallback(std::function<void()> callback) { resizeCallback_ = std::move(callback); }
    // 渲染通道因交换链格式变化而重建后回调（依赖该渲染通道的图形管线需在此重建）
    void SetRenderPassRecreateCallback(std::function<void()> callback)
    {
        renderPassRecreateCallback_ = std::move(callback);
    }

    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return renderPass_.renderPass; }
    [[nodiscard]] const Swapchain& GetSwapchain() const noexcept { return swapchain_; }
    [[nodiscard]] VkExtent2D Extent() const noexcept { return swapchain_.Extent(); }
    // 当前MSAA采样数（渲染通道与图形管线需保持一致）
    [[nodiscard]] VkSampleCountFlagBits SampleCount() const noexcept { return sampleCount_; }
    [[nodiscard]] static constexpr uint32_t MaxFramesInFlight() noexcept { return kMaxFrames; }

    // GPU 性能剖析器（设备不支持时间戳查询时为 nullptr）
    [[nodiscard]] Render::GpuProfiler* GetProfiler() const noexcept { return gpuProfiler_.get(); }

  private:
    static constexpr uint32_t kMaxFrames = 2;

    [[nodiscard]] VkFormat pickDepthFormat() const;
    [[nodiscard]] VkSampleCountFlagBits pickSampleCount() const;
    void createFrameResources();
    void destroyFrameResources();
    void createCommandResources();
    void createSyncObjects();
    void destroySyncObjects();
    void handleResize();

    // 延迟渲染：GBuffer 多渲染目标 + 双子通道渲染通道（几何 -> 延迟光照）
    void createDeferredResources();
    void destroyDeferredResources();
    void createDeferredRenderPass();
    void destroyDeferredRenderPass();
    void createDeferredFramebuffers();
    void destroyDeferredFramebuffers();

    const Context& ctx_;
    Window& window_;

    Swapchain swapchain_;
    Render::RenderPass renderPass_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits sampleCount_ = VK_SAMPLE_COUNT_1_BIT;

    // MSAA中间图像：整个交换链共用一套，随交换链重建
    Image msaaColorImage_;
    Image msaaDepthImage_;

    std::vector<VkFramebuffer> framebuffers_;

    // 延迟渲染状态：GBuffer 图像（每交换链图像一套）、双子通道渲染通道与帧缓冲
    bool deferredEnabled_ = false;
    VkRenderPass deferredRenderPass_ = VK_NULL_HANDLE;
    std::vector<Image> gAlbedoImages_;
    std::vector<Image> gNormalImages_;
    std::vector<Image> gPositionImages_;
    std::vector<Image> gDepthImages_;
    std::vector<VkFramebuffer> deferredFramebuffers_;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    // 同步对象：imageAvailable与栅栏按帧并行数、renderFinished按交换链图像数
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrame_ = 0;

    std::function<void()> resizeCallback_;
    std::function<void()> renderPassRecreateCallback_;
    std::unique_ptr<Render::GpuProfiler> gpuProfiler_;
};
} // namespace BigHero
