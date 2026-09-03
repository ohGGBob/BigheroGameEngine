#pragma once
#include "render/GBuffer.h"
#include "render/Image.h"
#include "render/PostProcessor.h"
#include "render/SSAO.h"
#include "render/SSR.h"
#include "render/Swapchain.h"
#include "render/gpu_profiler.h"
#include "render/render_pass.h"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
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
    //   几何 Pass 之后录制全屏延迟光照绘制（采样 GBuffer 纹理并输出到交换链）
    void DrawFrame(const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordScene,
                   const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordUi = {},
                   const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& prePass = {},
                   const std::function<void(VkCommandBuffer, uint32_t, uint32_t, VkExtent2D)>& recordLighting = {});

    // 延迟渲染开关：开启后 DrawFrame 走 GBuffer 几何 Pass + 独立延迟光照 Pass。
    // 启用时创建 GBuffer 图像与渲染通道；关闭时释放。
    void SetDeferred(bool enabled);
    [[nodiscard]] bool IsDeferred() const noexcept { return deferredEnabled_; }

    // 后处理开关：开启后场景渲染到离屏缓冲，经 Bloom+色调映射后输出到交换链。
    // 仅前向渲染模式支持（延迟模式下忽略）。默认关闭。
    void SetPostProcessing(bool enabled);
    [[nodiscard]] bool IsPostProcessing() const noexcept { return postProcessEnabled_; }
    [[nodiscard]] Render::PostProcessor* GetPostProcessor() noexcept { return &postProcessor_; }

    // 后处理相机参数（景深线性深度还原需要近/远平面），DrawFrame 之前每帧调用
    void SetPostProcessingCamera(float nearPlane, float farPlane) noexcept
    {
        postProcessNear_ = nearPlane;
        postProcessFar_ = farPlane;
    }

    // 升级 23：每帧设置运动模糊用相机重投影（prevVP × inverse(currVP)），DrawFrame 之前调用
    void SetMotionBlurCamera(const glm::mat4& prevVP, const glm::mat4& currVP) noexcept
    {
        postProcessor_.SetMotionBlurCamera(prevVP, currVP);
    }

    // SSAO 开关：仅延迟渲染模式下有效。开启后几何 Pass 与光照 Pass 之间插入 SSAO+模糊 Pass。
    void SetSSAO(bool enabled);
    [[nodiscard]] bool IsSSAO() const noexcept { return ssaoEnabled_; }
    [[nodiscard]] Render::SSAO* GetSSAO() noexcept { return &ssao_; }
    // 每帧设置 SSAO 用相机参数（viewProj + 相机世界坐标），在 DrawFrame 之前调用
    void SetSSAOCamera(const glm::mat4& viewProj, const glm::vec3& cameraPos) noexcept
    {
        ssaoViewProj_ = viewProj;
        ssaoCameraPos_ = cameraPos;
    }

    // SSR 开关：仅延迟渲染模式下有效。开启后光照 Pass 输出到离屏缓冲，经 SSR 反射后合成到交换链。
    void SetSSR(bool enabled);
    [[nodiscard]] bool IsSSR() const noexcept { return ssrEnabled_; }
    [[nodiscard]] Render::SSR* GetSSR() noexcept { return &ssr_; }
    void SetSSRCamera(const glm::mat4& viewProj, const glm::vec3& cameraPos) noexcept
    {
        ssrViewProj_ = viewProj;
        ssrCameraPos_ = cameraPos;
    }

    // 延迟渲染通道与 GBuffer 视图（供外部创建管线/更新描述符集）
    [[nodiscard]] VkRenderPass GetDeferredRenderPass() const noexcept { return deferredRenderPass_; }
    [[nodiscard]] VkRenderPass GetLightingRenderPass() const noexcept { return lightingRenderPass_; }
    [[nodiscard]] VkImageView GBufferAlbedoView(uint32_t imageIndex) const noexcept;
    [[nodiscard]] VkImageView GBufferNormalView(uint32_t imageIndex) const noexcept;
    [[nodiscard]] VkImageView GBufferPositionView(uint32_t imageIndex) const noexcept;
    [[nodiscard]] VkImageView GetDummyWhiteView() const noexcept { return dummyWhiteImage_.View(); }

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
    void createDummyWhiteImage();
    void handleResize();

    // 延迟渲染：GBuffer 多渲染目标 + 几何/光照分离渲染通道
    void createDeferredResources();
    void destroyDeferredResources();
    void createDeferredRenderPass();
    void destroyDeferredRenderPass();
    void createLightingRenderPass();
    void destroyLightingRenderPass();
    void createDeferredFramebuffers();
    void destroyDeferredFramebuffers();
    void createCompositeResources();
    void destroyCompositeResources();

    // 后处理：离屏场景帧缓冲 + PostProcessor
    void createOffscreenFramebuffer();
    void destroyOffscreenFramebuffer();

    const Context& ctx_;
    Window& window_;

    Swapchain swapchain_;
    Render::RenderPass renderPass_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits sampleCount_ = VK_SAMPLE_COUNT_1_BIT;

    // MSAA中间图像：整个交换链共用一套，随交换链重建
    Image msaaColorImage_;
    Image msaaDepthImage_;
    // 1x1 白色纹理（SSAO 关闭时作为 AO 回退，确保 AO=1 无效果）
    Image dummyWhiteImage_;

    std::vector<VkFramebuffer> framebuffers_;

    // 延迟渲染状态：GBuffer 图像（每交换链图像一套）、几何通道与光照通道
    bool deferredEnabled_ = false;
    VkRenderPass deferredRenderPass_ = VK_NULL_HANDLE;
    VkRenderPass lightingRenderPass_ = VK_NULL_HANDLE;
    std::vector<Image> gAlbedoImages_;
    std::vector<Image> gNormalImages_;
    std::vector<Image> gPositionImages_;
    std::vector<Image> gDepthImages_;
    std::vector<VkFramebuffer> deferredFramebuffers_;
    std::vector<VkFramebuffer> lightingFramebuffers_;

    // SSAO 状态（仅延迟模式下使用）
    bool ssaoEnabled_ = false;
    Render::SSAO ssao_;
    glm::mat4 ssaoViewProj_{1.0f};
    glm::vec3 ssaoCameraPos_{0.0f};

    // SSR 状态（仅延迟模式下使用）
    bool ssrEnabled_ = false;
    Render::SSR ssr_;
    glm::mat4 ssrViewProj_{1.0f};
    glm::vec3 ssrCameraPos_{0.0f};

    // 延迟离屏颜色缓冲（光照 Pass 输出，SSR/合成 Pass 采样）
    std::unique_ptr<Image> offscreenColorImage_;
    VkRenderPass compositeRenderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> compositeFramebuffers_;
    std::unique_ptr<Render::GraphicsPipeline> compositePipeline_;
    VkDescriptorSetLayout compositeLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool compositeDescPool_ = VK_NULL_HANDLE;
    VkDescriptorSet compositeSet_ = VK_NULL_HANDLE;
    VkSampler compositeSampler_ = VK_NULL_HANDLE;

    // 后处理状态
    bool postProcessEnabled_ = false;
    VkFramebuffer offscreenFramebuffer_ = VK_NULL_HANDLE;
    Render::PostProcessor postProcessor_;
    float postProcessNear_ = 0.1f;  // 升级 22：后处理相机近平面（景深用）
    float postProcessFar_ = 500.0f; // 升级 22：后处理相机远平面（景深用）

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
