#pragma once
#include "render/CubeShadowMap.h"
#include "render/FrameStaging.h"
#include "render/GBuffer.h"
#include "render/Image.h"
#include "render/ParallelCommandRecorder.h"
#include "render/PostProcessor.h"
#include "render/RenderGraph.h"
#include "render/SSAO.h"
#include "render/SSR.h"
#include "render/Swapchain.h"
#include "render/TransientAllocator.h"
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
    // Headless mode: no swapchain/window (for CI validation)
    explicit Renderer(const Context& ctx);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Allow move construction for std::optional emplace
    Renderer(Renderer&&) = default;
    Renderer& operator=(Renderer&&) = default;

    // 每帧调用：处理尺寸变化与最小化等待，随后录制并提交一帧
    // prePass(cmd, frameIndex, extent)（可选）：主渲染通道之前的深度预通道（阴影贴图等）
    // recordScene(cmd, frameIndex, extent)：由外部负责绑定管线、描述符与几何体并下达绘制命令
    // frameIndex用于选取该帧并行槽位独立的UBO/描述符
    // recordUi(cmd, imageIndex, extent)（可选）：场景通道结束后在UI覆盖层通道中录制界面
    // recordLighting(cmd, frameIndex, imageIndex, extent)（可选）：延迟渲染模式下，
    //   几何 Pass 之后录制全屏延迟光照绘制（采样 GBuffer 纹理并输出到交换链）
    // recordTransparent(cmd, frameIndex, imageIndex, extent)（可选）：延迟渲染模式下，
    //   光照 Pass 之后在透明叠加通道中录制 BLEND/加性自发光物体（深度只读测试，混合输出）
    void DrawFrame(const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordScene,
                   const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordUi = {},
                   const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& prePass = {},
                   const std::function<void(VkCommandBuffer, uint32_t, uint32_t, VkExtent2D)>& recordLighting = {},
                   const std::function<void(VkCommandBuffer, uint32_t, uint32_t, VkExtent2D)>& recordTransparent = {},
                   const std::function<void(Render::ParallelCommandRecorder&, uint32_t)>& parallelPrePass = {});

    // 延迟渲染开关：开启后 DrawFrame 走 GBuffer 几何 Pass + 独立延迟光照 Pass。
    // 启用时创建 GBuffer 图像与渲染通道；关闭时释放。
    void SetDeferred(bool enabled);
    [[nodiscard]] bool IsDeferred() const noexcept { return deferredEnabled_; }

    // 后处理开关：开启后场景渲染到离屏缓冲，经 Bloom+色调映射后输出到交换链。
    // 仅前向渲染模式支持（延迟模式下忽略）。默认关闭。
    void SetPostProcessing(bool enabled);
    [[nodiscard]] bool IsPostProcessing() const noexcept { return postProcessEnabled_; }
    [[nodiscard]] Render::PostProcessor* GetPostProcessor() noexcept { return &postProcessor_; }

    // 场景主通道颜色附件格式：后处理开启时用 HDR（R16G16B16A16_SFLOAT）承载
    // 线性场景颜色（合成端统一 ACES）；关闭时直通交换链格式（LDR，片元内 ACES）。
    // headless 无交换链，返回占位 SRGB 格式。
    [[nodiscard]] VkFormat SceneColorFormat() const noexcept;

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
    // P0-3 Commit3：deferred 链曝光收敛到 composite（与 forward+PP 合成端同源，
    // 由 Application 每帧从 lightParams.exposure 同步；链末端统一乘曝光 + ACES）
    void SetExposure(float exposure) noexcept { exposure_ = exposure; }

    // 延迟渲染通道与 GBuffer 视图（供外部创建管线/更新描述符集）
    [[nodiscard]] VkRenderPass GetDeferredRenderPass() const noexcept { return deferredRenderPass_; }
    [[nodiscard]] VkRenderPass GetLightingRenderPass() const noexcept { return lightingRenderPass_; }
    // 透明叠加通道（延迟模式）：颜色=离屏 HDR（LOAD+混合），深度=GBuffer 深度只读测试
    [[nodiscard]] VkRenderPass GetTransparentRenderPass() const noexcept { return transparentRenderPass_; }
    [[nodiscard]] VkImageView GBufferAlbedoView(uint32_t imageIndex) const noexcept;
    [[nodiscard]] VkImageView GBufferNormalView(uint32_t imageIndex) const noexcept;
    [[nodiscard]] VkImageView GBufferPositionView(uint32_t imageIndex) const noexcept;
    // GBuffer 图像是否已完成 transient 池绑定（视图就绪）。未就绪时不应把 .View() 写入描述符集
    [[nodiscard]] bool TransientViewsReady() const noexcept { return transientBound_; }
    [[nodiscard]] VkImageView GetDummyWhiteView() const noexcept { return dummyWhiteImage_.View(); }

    // 交换链重建完成后回调（供覆盖层等依赖交换链图像的资源重建）
    void SetResizeCallback(std::function<void()> callback) { resizeCallback_ = std::move(callback); }
    // 渲染通道因交换链格式变化而重建后回调（依赖该渲染通道的图形管线需在此重建）
    void SetRenderPassRecreateCallback(std::function<void()> callback)
    {
        renderPassRecreateCallback_ = std::move(callback);
    }
    // GBuffer/SSR 图像完成 transient 池显存绑定后回调：此时图像视图才有效，外部可安全
    // 把它们写入描述符集（早于此调用取 .View() 会得到 VK_NULL_HANDLE，违反 VUID-01020）
    void SetTransientBoundCallback(std::function<void()> callback) { transientBoundCallback_ = std::move(callback); }

    [[nodiscard]] VkRenderPass GetRenderPass() const noexcept { return renderPass_.renderPass; }
    [[nodiscard]] const Swapchain& GetSwapchain() const noexcept { return swapchain_; }
    [[nodiscard]] VkExtent2D Extent() const noexcept { return swapchain_.Extent(); }
    // 当前MSAA采样数（渲染通道与图形管线需保持一致）
    [[nodiscard]] VkSampleCountFlagBits SampleCount() const noexcept { return sampleCount_; }
    [[nodiscard]] static constexpr uint32_t MaxFramesInFlight() noexcept { return kMaxFrames; }

    // 帧瞬态上传池：每帧实例/粒子数据经此中转拷入设备本地缓冲（替代逐帧 staging 分配+一次性提交）
    [[nodiscard]] Render::FrameStaging& Staging() noexcept { return frameStaging_; }

    // GPU 性能剖析器（设备不支持时间戳查询时为 nullptr）
    [[nodiscard]] Render::GpuProfiler* GetProfiler() const noexcept { return gpuProfiler_.get(); }

  private:
    static constexpr uint32_t kMaxFrames = 2;
    // 帧瞬态上传池每槽位容量（实例+粒子数据峰值远小于此；满载约 4 万实例）
    static constexpr VkDeviceSize kFrameStagingBytes = 4ull * 1024 * 1024;

    [[nodiscard]] VkFormat pickDepthFormat() const;
    [[nodiscard]] VkSampleCountFlagBits pickSampleCount() const;
    void createFrameResources();
    void destroyFrameResources();
    void createCommandResources();
    void createSyncObjects();
    void destroySyncObjects();
    void createDummyWhiteImage();
    void handleResize();
    // 交换链重建后的 per-image 资源一致性校验：把尺寸应等于 swapchain_.ImageCount() 的
    // 各向量逐一对账（framebuffers_/信号量/延迟帧缓冲与 GBuffer 图像/合成帧缓冲），
    // 差异日志告警；延迟模式与合成资源在一致性破坏时按需自愈重建（handleResize 末尾调用）。
    // 防止未来新增 per-image 资源时再次漏掉重建链（历史上曾导致 imageIndex 裸下标越界）。
    void validateSwapchainDependentResources();

    // 延迟渲染：GBuffer 多渲染目标 + 几何/光照分离渲染通道
    void createDeferredResources();
    void destroyDeferredResources();
    // TransientAllocator 池化绑定：GBuffer/SSR 图像按生命周期别名共享显存槽位
    //（图像集变化后由 DrawFrame 开头的 transientBindDirty_ 触发重绑）
    void bindTransientImages();
    void createDeferredRenderPass();
    void destroyDeferredRenderPass();
    void createLightingRenderPass();
    void destroyLightingRenderPass();
    void createTransparentRenderPass();
    void destroyTransparentRenderPass();
    void createDeferredFramebuffers();
    // 取 GBuffer 图像视图创建几何/光照/透明帧缓冲（须在 transient 池绑定显存之后调用，
    // 以严格满足 VUID-01020：图像 view 必须在绑内存后创建）
    void createDeferredFramebufferObjects();
    void destroyDeferredFramebuffers();
    void createCompositeResources();
    void destroyCompositeResources();
    void createCompositeRenderPass();
    void destroyCompositeRenderPass();
    void createCompositeFramebuffers();
    void destroyCompositeFramebuffers();

    // 后处理：离屏场景帧缓冲 + PostProcessor
    void createOffscreenFramebuffer();
    void destroyOffscreenFramebuffer();

    void initCommon();

    const Context& ctx_;
    Window* window_ = nullptr;

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
    // 透明叠加通道：渲染通道（颜色=离屏 HDR LOAD+混合，深度=GBuffer 深度只读测试）与
    // 帧缓冲（每交换链图像一套，延迟模式专用）
    VkRenderPass transparentRenderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> transparentFramebuffers_;

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
    float exposure_ = 1.0f; // deferred 合成曝光（每帧由 Application 同步）

    // 延迟离屏颜色缓冲（光照 Pass 输出，SSR/合成 Pass 采样）
    std::unique_ptr<Image> offscreenColorImage_;
    VkRenderPass compositeRenderPass_ = VK_NULL_HANDLE;
    // 合成帧缓冲：前向模式下允许为空或过期（合成 pass 仅在 deferredEnabled_ 时创建并消费）
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

    // 多线程命令录制：无依赖 pass（如点光源立方体阴影 6 面）并行录制到独立 command buffer
    Render::ParallelCommandRecorder parallelRecorder_;

    // 帧瞬态上传池：每槽位常驻 host-visible arena，帧栅栏后整帧回收
    Render::FrameStaging frameStaging_;

    // 渲染图 transient 内存报告（生命周期/别名槽位/理论节省，首次构建打印一次）
    void logTransientMemoryReport(const Render::RenderGraph& graph);

    // 帧渲染图：声明式 pass 链 + 自动跨 pass 布局转换/同步
    Render::RenderGraph frameGraph_;

    // 瞬态显存池：GBuffer/SSR 离屏图像按生命周期别名复用 device-local 显存。
    // 注意成员析构按声明逆序进行：本成员声明在图像成员之后 => 析构先于图像，故
    // 不能依赖 RAII 顺序——~Renderer 已显式先调 ssr_.Destroy() + destroyDeferredResources()
    // 销毁绑定图像并释放池，避免池显存早于图像/帧缓冲销毁（悬垂引用）。
    // GBuffer 图像以未绑定态创建（CreateUnbound），图像集变化（初始化/开关 SSR/重建）后
    // 由 DrawFrame 开头的 transientBindDirty_ 触发 bindTransientImages 统一分配共享槽位。
    Render::TransientAllocator transientAlloc_;
    bool transientBindDirty_ = false;              // GBuffer/SSR 图像集变化，待下一帧重绑
    bool transientBound_ = false;                  // 当前池绑定有效（运行中开关 SSR 需重建 GBuffer 后重绑）
    std::function<void()> transientBoundCallback_; // 池绑定完成后回调（外部据此更新描述符集）
};
} // namespace BigHero
