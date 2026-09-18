#include "render/Renderer.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "core/VkUtils.h"
#include "platform/Window.h"
#include "render/Context.h"
#include "render/image.h"
#include "render/pipeline.h"

#include <array>
#include <memory>
#include <stdexcept>

namespace BigHero
{
Renderer::Renderer(const Context& ctx, Window& window)
    : ctx_(ctx), window_(&window), depthFormat_(pickDepthFormat()), sampleCount_(pickSampleCount())
{
    if (ctx_.IsHeadless())
    {
        // Headless 上下文：无窗口表面，跳过交换链，用占位颜色格式构建渲染通道
        renderPass_.Create(ctx_.Device(), VK_FORMAT_B8G8R8A8_SRGB, depthFormat_, sampleCount_);
        createDeferredRenderPass();
        createLightingRenderPass();
        createTransparentRenderPass();
        createFrameResources();
        createCommandResources();
        createDummyWhiteImage();
        createSyncObjects();
        initCommon();
        LOG_INFO("渲染器初始化完成 (headless)，帧并行数: " << kMaxFrames << "，MSAA采样数: "
                                                           << static_cast<uint32_t>(sampleCount_) << "x");
        return;
    }

    swapchain_.Create(ctx_, window);
    renderPass_.Create(ctx_.Device(), swapchain_.Format(), depthFormat_, sampleCount_);
    // 延迟渲染：几何通道（GBuffer）与光照通道始终创建，供管线在启动时构建；
    // GBuffer 图像与帧缓冲仅在启用延迟模式时创建
    createDeferredRenderPass();
    createLightingRenderPass();
    createTransparentRenderPass();
    createFrameResources();
    createCommandResources();
    createDummyWhiteImage();
    createSyncObjects();
    initCommon();

    LOG_INFO("渲染器初始化完成，帧并行数: " << kMaxFrames << "，MSAA采样数: " << static_cast<uint32_t>(sampleCount_)
                                            << "x");
}

Renderer::Renderer(const Context& ctx)
    : ctx_(ctx), window_(nullptr), depthFormat_(pickDepthFormat()), sampleCount_(pickSampleCount())
{
    // Headless: skip swapchain and window-dependent resources
    renderPass_.Create(ctx_.Device(), VK_FORMAT_B8G8R8A8_SRGB, depthFormat_, sampleCount_);
    createDeferredRenderPass();
    createLightingRenderPass();
    createTransparentRenderPass();
    createFrameResources();
    createCommandResources();
    createDummyWhiteImage();
    createSyncObjects();
    initCommon();

    LOG_INFO("渲染器初始化完成 (headless)，帧并行数: " << kMaxFrames << "，MSAA采样数: "
                                                       << static_cast<uint32_t>(sampleCount_) << "x");
}

void Renderer::initCommon()
{
    // GPU 性能剖析器（设备不支持时间戳查询时自动跳过）
    if (ctx_.GraphicsTimestampSupported())
    {
        gpuProfiler_ = std::make_unique<Render::GpuProfiler>();
        gpuProfiler_->Init(ctx_.Device(), kMaxFrames, ctx_.TimestampPeriod());
        LOG_INFO("GPU 性能剖析已启用（时间戳周期 " << ctx_.TimestampPeriod() << " ns/tick）");
    }
    else
    {
        LOG_INFO("当前设备不支持图形时间戳查询，GPU 性能剖析已禁用");
    }
}

Renderer::~Renderer()
{
    ctx_.WaitIdle();
    ssao_.Destroy();
    // SSR 帧缓冲引用其反射/模糊图像（transient 池显存）。必须先于 destroyDeferredResources()
    // 释放——后者会销毁池绑定图像并释放池显存，若此时 SSR 帧缓冲仍存活，销毁帧缓冲时
    // 其附件图像已失效（VUID 违规 + 悬垂引用）。
    ssr_.Destroy();
    destroyDeferredResources();
    destroyDeferredRenderPass();
    destroyLightingRenderPass();
    destroyTransparentRenderPass();
    destroySyncObjects();
    destroyFrameResources();
    parallelRecorder_.Destroy();
    frameStaging_.Destroy();
    if (commandPool_ != VK_NULL_HANDLE)
        vkDestroyCommandPool(ctx_.Device(), commandPool_, nullptr);
    renderPass_.Release();
    if (!ctx_.IsHeadless())
        swapchain_.Destroy();
}

VkFormat Renderer::pickDepthFormat() const
{
    const VkFormat format = FindSupportedFormat(
        ctx_.PhysicalDevice(),
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM},
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    if (format == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("未找到可用的深度缓冲格式");
    return format;
}

// 依据颜色与深度格式支持的帧缓冲位数选择MSAA采样数，优先4x
VkSampleCountFlagBits Renderer::pickSampleCount() const
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx_.PhysicalDevice(), &props);

    const VkSampleCountFlags colorBits = props.limits.framebufferColorSampleCounts;
    const VkSampleCountFlags depthBits = props.limits.framebufferDepthSampleCounts;

    const VkSampleCountFlagBits candidates[] = {VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT};
    for (VkSampleCountFlagBits samples : candidates)
    {
        if ((colorBits & samples) && (depthBits & samples))
            return samples;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

void Renderer::createCommandResources()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = ctx_.GraphicsFamily();
    VK_CHECK(vkCreateCommandPool(ctx_.Device(), &poolInfo, nullptr, &commandPool_), "创建图形命令池");

    commandBuffers_.resize(kMaxFrames);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kMaxFrames;
    VK_CHECK(vkAllocateCommandBuffers(ctx_.Device(), &allocInfo, commandBuffers_.data()), "分配帧命令缓冲");

    // 多线程命令录制：6 个工作线程（点光源立方体阴影面数），并行帧槽与主帧一致
    parallelRecorder_.Create(ctx_, CubeShadowMap::kFaceCount, kMaxFrames);

    // 帧瞬态上传池：每槽位常驻 host-visible arena，供每帧实例/粒子数据中转
    frameStaging_.Create(ctx_, kMaxFrames, kFrameStagingBytes);
}

void Renderer::createDummyWhiteImage()
{
    dummyWhiteImage_.Destroy();
    dummyWhiteImage_.Create(ctx_, 1, 1, VK_FORMAT_R8_UNORM,
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    // 用一次性命令缓冲清除为白色（1.0）
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(ctx_.Device(), &allocInfo, &cmd), "分配白色纹理命令缓冲");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = dummyWhiteImage_.Get();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    VkClearColorValue white{.float32 = {1.0f, 1.0f, 1.0f, 1.0f}};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, dummyWhiteImage_.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &range);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx_.GraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_.GraphicsQueue());
    vkFreeCommandBuffers(ctx_.Device(), commandPool_, 1, &cmd);
}

void Renderer::DrawFrame(const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordScene,
                         const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordUi,
                         const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& prePass,
                         const std::function<void(VkCommandBuffer, uint32_t, uint32_t, VkExtent2D)>& recordLighting,
                         const std::function<void(VkCommandBuffer, uint32_t, uint32_t, VkExtent2D)>& recordTransparent,
                         const std::function<void(Render::ParallelCommandRecorder&, uint32_t)>& parallelPrePass)
{
    // Headless mode: skip window/swapchain operations
    if (ctx_.IsHeadless())
    {
        LOG_INFO("Headless DrawFrame: skipping window/swapchain operations");
        return;
    }

    // 窗口最小化时挂起等待，直到恢复有效尺寸
    while (true)
    {
        const auto [width, height] = window_->GetFramebufferSize();
        if (width > 0 && height > 0)
            break;
        window_->WaitEvents();
    }

    if (window_->ConsumeResizedFlag())
        handleResize();

    const VkDevice device = ctx_.Device();
    const VkFence inFlightFence = inFlightFences_[currentFrame_];
    VK_CHECK(vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX), "等待帧栅栏");

    // GBuffer/SSR 图像集变化（初始化/重建/SSR 开关）：统一分配 transient 池共享槽位。
    // 触发路径（createDeferredFramebuffers/SetSSR/handleResize）均已 WaitIdle 且旧池绑定图像销毁，
    // 此处 vkFreeMemory/重绑安全。
    if (transientBindDirty_)
    {
        transientBindDirty_ = false;
        bindTransientImages();
    }

    // 栅栏已等待：GPU 读完本槽位上一帧的瞬态切片，整帧回收（bump 游标归零）安全
    frameStaging_.Reset(currentFrame_);

    // 回读上一轮已完成的 GPU 时间戳（此时该帧栅栏已就绪）
    if (gpuProfiler_)
        gpuProfiler_->Resolve(currentFrame_);

    uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(
        device, swapchain_.Handle(), UINT64_MAX, imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
    {
        handleResize();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
        VK_CHECK(acquireResult, "获取交换链图像");

    // 防御性一致性闸门：以 imageIndex 为下标的全部 per-image 资源必须与当前交换链图像数
    // 齐套（createFrameResources/createSyncObjects/createDeferredFramebuffers 维护；
    // handleResize 末尾另有校验自愈）。任一失配（历史上曾致 vector subscript out of range
    // 匿名断言崩溃）时，复用 OUT_OF_DATE 的恢复路径：重建资源并弃本帧，下一帧正常渲染，
    // 同时把失真降级为日志而非进程崩溃。
    const bool perImageResourcesReady =
        imageIndex < framebuffers_.size() && imageIndex < renderFinishedSemaphores_.size() &&
        (!deferredEnabled_ ||
         (imageIndex < gAlbedoImages_.size() && imageIndex < gNormalImages_.size() &&
          imageIndex < gPositionImages_.size() && imageIndex < gDepthImages_.size() &&
          imageIndex < deferredFramebuffers_.size() && imageIndex < lightingFramebuffers_.size() &&
          imageIndex < transparentFramebuffers_.size() && imageIndex < compositeFramebuffers_.size() &&
          deferredFramebuffers_[imageIndex] != VK_NULL_HANDLE && lightingFramebuffers_[imageIndex] != VK_NULL_HANDLE &&
          transparentFramebuffers_[imageIndex] != VK_NULL_HANDLE &&
          compositeFramebuffers_[imageIndex] != VK_NULL_HANDLE));
    if (!perImageResourcesReady)
    {
        static bool sPerImageWarned = false;
        if (!sPerImageWarned)
        {
            sPerImageWarned = true;
            LOG_WARN("per-image 渲染资源与交换链不一致（imageIndex=" << imageIndex
                                                                     << "），触发资源自愈重建（后续同类告警已抑制）");
        }
        handleResize();
        return;
    }

    VK_CHECK(vkResetFences(device, 1, &inFlightFence), "重置帧栅栏");

    using Render::RGUsage;
    using Render::RGUsageDecl;
    using enum Render::RGUsage;

    // ---- 录制命令 ----
    VkCommandBuffer cmd = commandBuffers_[currentFrame_];
    VK_CHECK(vkResetCommandBuffer(cmd, 0), "重置命令缓冲");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "开始录制命令缓冲");

    // 重置本帧时间戳查询池
    if (gpuProfiler_)
        gpuProfiler_->Reset(cmd, currentFrame_);

    // 多线程命令录制：无依赖 pass（点光源立方体阴影 6 面）并行录制到独立 command buffer，
    // 提交时以同一 vkQueueSubmit 的 buffer 数组前置到主命令缓冲之前顺序执行
    if (parallelPrePass)
    {
        parallelRecorder_.Reset(currentFrame_);
        parallelPrePass(parallelRecorder_, currentFrame_);
    }

    // ---- 构建帧渲染图：声明式 pass 链 + 自动跨 pass 布局转换/同步 ----
    // 布局/依赖显式化：新增 pass 只需 RegisterImage + AddPass，跨 pass 的 barrier 由渲染图推导插入，
    // 取代硬编码 pass 顺序中的人工 barrier（如 PostProcessor 的深度布局转换）。
    frameGraph_.Clear();

    const VkExtent2D extent = swapchain_.Extent();
    const VkImage swapImage = swapchain_.Images()[imageIndex];

    // 稳定布局的外部资源（黑盒 pass 输出，内容跨帧有效，渲染图不干预其内部转换）
    frameGraph_.RegisterImage("dummyWhite", dummyWhiteImage_.Get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    {
        const VkDeviceSize w = extent.width;
        const VkDeviceSize h = extent.height;
        if (!deferredEnabled_)
        {
            if (postProcessEnabled_)
            {
                // 前向+后处理：离屏 MSAA 颜色（最大）、解析图、场景深度
                frameGraph_.RegisterImage("offscreenMsaa", postProcessor_.OffscreenMsaaColorImage(),
                                          VK_IMAGE_LAYOUT_UNDEFINED,
                                          w * h * 8ull * static_cast<uint32_t>(sampleCount_));
                frameGraph_.RegisterImage("offscreenResolve", postProcessor_.OffscreenResolveImage(),
                                          VK_IMAGE_LAYOUT_UNDEFINED, w * h * 8ull);
                frameGraph_.RegisterImage("sceneDepth", msaaDepthImage_.Get(), VK_IMAGE_LAYOUT_UNDEFINED, w * h * 4ull);
            }
        }
        else
        {
            // 延迟：GBuffer（albedo/normal/position）+ 几何深度 + 半分辨率后处理图。
            // GBuffer/SSR 图像显存由 transient 池共享（frameSharedMemory）：各交换链槽位实例
            // 绑定同一偏移，首用屏障源取组内读写掩码并集，覆盖上一帧同槽位实例的残留访问。
            frameGraph_.RegisterImage("gAlbedo", gAlbedoImages_[imageIndex].Get(), VK_IMAGE_LAYOUT_UNDEFINED,
                                      w * h * 8ull, true);
            frameGraph_.RegisterImage("gNormal", gNormalImages_[imageIndex].Get(), VK_IMAGE_LAYOUT_UNDEFINED,
                                      w * h * 8ull, true);
            frameGraph_.RegisterImage("gPosition", gPositionImages_[imageIndex].Get(), VK_IMAGE_LAYOUT_UNDEFINED,
                                      w * h * 8ull, true);
            const uint32_t gDepthIdx = frameGraph_.RegisterImage("gDepth", gDepthImages_[imageIndex].Get(),
                                                                 VK_IMAGE_LAYOUT_UNDEFINED, w * h * 4ull, true);
            if (ssaoEnabled_ && ssao_.IsValid())
                frameGraph_.RegisterImage("ssaoAO", ssao_.GetAOImage(), VK_IMAGE_LAYOUT_UNDEFINED, (w / 2) * (h / 2));
            if (ssrEnabled_ && ssr_.IsValid())
            {
                // SSR 反射图别名共享 GBuffer 深度槽位：生命周期不重叠
                // （gDepth [gbuffer,transparent] vs ssrReflection [ssr,composite]，usages 见 ssr/composite pass）
                const uint32_t ssrReflIdx =
                    frameGraph_.RegisterImage("ssrReflection", ssr_.GetReflectionImage(), VK_IMAGE_LAYOUT_UNDEFINED,
                                              (w / 2) * (h / 2) * 8ull, true);
                frameGraph_.RegisterImage("ssrBlur", ssr_.GetBlurImage(), VK_IMAGE_LAYOUT_UNDEFINED,
                                          (w / 2) * (h / 2) * 8ull, true);
                frameGraph_.DeclareAlias(gDepthIdx, ssrReflIdx);
            }
        }
    }
    // 1) 深度预通道（阴影贴图等）——黑盒 pass：内部布局自洽，渲染图不声明其资源
    frameGraph_.AddPass("shadow",
                        [&]
                        {
                            if (gpuProfiler_)
                                gpuProfiler_->Write(cmd, currentFrame_, 0);
                            if (prePass)
                                prePass(cmd, currentFrame_, extent);
                            if (gpuProfiler_)
                                gpuProfiler_->Write(cmd, currentFrame_, 1);
                        });

    // 前向场景通道
    if (!deferredEnabled_)
    {
        const bool toOffscreen = postProcessEnabled_;
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color.float32[0] = 0.08f;
        clearValues[0].color.float32[1] = 0.09f;
        clearValues[0].color.float32[2] = 0.12f;
        clearValues[0].color.float32[3] = 1.0f;
        clearValues[1].depthStencil = {1.0f, 0};

        if (toOffscreen)
        {
            // 场景渲染到离屏缓冲：MSAA 颜色 + 解析（供后处理采样），深度最终供景深/运动模糊采样
            frameGraph_.AddPass(
                "scene",
                [&]
                {
                    VkRenderPassBeginInfo passInfo{};
                    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    passInfo.renderPass = renderPass_.renderPass;
                    passInfo.framebuffer = offscreenFramebuffer_;
                    passInfo.renderArea.offset = {0, 0};
                    passInfo.renderArea.extent = extent;
                    passInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
                    passInfo.pClearValues = clearValues.data();
                    vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
                    recordScene(cmd, currentFrame_, extent);
                    vkCmdEndRenderPass(cmd);
                },
                {
                    {postProcessor_.OffscreenMsaaColorImage(), RGUsage::ColorAttachment,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                    {postProcessor_.OffscreenResolveImage(), RGUsage::ColorAttachment,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                    {msaaDepthImage_.Get(), RGUsage::DepthAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
                });

            // 后处理链（黑盒：内部 DoF/MB/Bloom 自洽）：输入场景颜色+深度，输出交换链
            frameGraph_.AddPass(
                "post",
                [&]
                {
                    postProcessor_.RecordBloom(cmd, imageIndex, extent, postProcessNear_, postProcessFar_);
                    if (gpuProfiler_)
                        gpuProfiler_->Write(cmd, currentFrame_, 2);
                },
                {
                    {postProcessor_.OffscreenResolveImage(), RGUsage::SampledRead},
                    {msaaDepthImage_.Get(), RGUsage::DepthReadOnly},
                    {swapImage, RGUsage::PresentSrc, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                });
        }
        else
        {
            frameGraph_.AddPass(
                "scene",
                [&]
                {
                    VkRenderPassBeginInfo passInfo{};
                    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    passInfo.renderPass = renderPass_.renderPass;
                    passInfo.framebuffer = framebuffers_[imageIndex];
                    passInfo.renderArea.offset = {0, 0};
                    passInfo.renderArea.extent = extent;
                    passInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
                    passInfo.pClearValues = clearValues.data();
                    vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
                    recordScene(cmd, currentFrame_, extent);
                    vkCmdEndRenderPass(cmd);
                    if (gpuProfiler_)
                        gpuProfiler_->Write(cmd, currentFrame_, 2);
                },
                {
                    {swapImage, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                    {msaaDepthImage_.Get(), RGUsage::DepthAttachment, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
                });
        }
    }

    // 延迟渲染：几何 Pass -> (SSAO) -> 光照 Pass -> (SSR) -> 合成 Pass
    if (deferredEnabled_)
    {
        // ---- 几何 Pass：写 GBuffer ----
        frameGraph_.AddPass(
            "gBuffer",
            [&]
            {
                std::array<VkClearValue, 4> deferredClears{};
                deferredClears[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
                deferredClears[1].color = {0.0f, 0.0f, 0.0f, 0.0f};
                deferredClears[2].color = {0.0f, 0.0f, 0.0f, 0.0f};
                deferredClears[3].depthStencil = {1.0f, 0};

                VkRenderPassBeginInfo dPassInfo{};
                dPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                dPassInfo.renderPass = deferredRenderPass_;
                dPassInfo.framebuffer = deferredFramebuffers_[imageIndex];
                dPassInfo.renderArea.offset = {0, 0};
                dPassInfo.renderArea.extent = extent;
                dPassInfo.clearValueCount = static_cast<uint32_t>(deferredClears.size());
                dPassInfo.pClearValues = deferredClears.data();

                vkCmdBeginRenderPass(cmd, &dPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                recordScene(cmd, currentFrame_, extent);
                vkCmdEndRenderPass(cmd);
            },
            {
                {gAlbedoImages_[imageIndex].Get(), RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {gNormalImages_[imageIndex].Get(), RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {gPositionImages_[imageIndex].Get(), RGUsage::ColorAttachment,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {gDepthImages_[imageIndex].Get(), RGUsage::DepthAttachment,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
            });

        // ---- SSAO Pass（几何之后、光照之前）----
        if (ssaoEnabled_ && ssao_.IsValid())
        {
            frameGraph_.AddPass(
                "ssao",
                [&]
                {
                    ssao_.RecordPass(cmd, GBufferPositionView(imageIndex), GBufferNormalView(imageIndex), ssaoViewProj_,
                                     ssaoCameraPos_);
                },
                {
                    {gPositionImages_[imageIndex].Get(), RGUsage::SampledRead},
                    {gNormalImages_[imageIndex].Get(), RGUsage::SampledRead},
                    {ssao_.GetAOImage(), RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                });
        }

        // ---- 光照 Pass：采样 GBuffer (+AO) 输出到离屏 HDR 缓冲 ----
        std::vector<RGUsageDecl> lightUsages;
        lightUsages.reserve(6);
        lightUsages.push_back({gAlbedoImages_[imageIndex].Get(), RGUsage::SampledRead});
        lightUsages.push_back({gNormalImages_[imageIndex].Get(), RGUsage::SampledRead});
        lightUsages.push_back({gPositionImages_[imageIndex].Get(), RGUsage::SampledRead});
        if (ssaoEnabled_ && ssao_.IsValid())
            lightUsages.push_back({ssao_.GetAOImage(), RGUsage::SampledRead});
        // 声明光照输出（离屏 HDR）：渲染图据此跟踪写入，为透明/SSR/合成 Pass 推导同步
        lightUsages.push_back({offscreenColorImage_->Get(), RGUsage::ColorAttachment,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        frameGraph_.AddPass(
            "lighting",
            [&]
            {
                std::array<VkClearValue, 1> lightClears{};
                lightClears[0].color = {0.08f, 0.09f, 0.12f, 1.0f};

                VkRenderPassBeginInfo lPassInfo{};
                lPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                lPassInfo.renderPass = lightingRenderPass_;
                lPassInfo.framebuffer = lightingFramebuffers_[imageIndex];
                lPassInfo.renderArea.offset = {0, 0};
                lPassInfo.renderArea.extent = extent;
                lPassInfo.clearValueCount = static_cast<uint32_t>(lightClears.size());
                lPassInfo.pClearValues = lightClears.data();

                vkCmdBeginRenderPass(cmd, &lPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                if (recordLighting)
                    recordLighting(cmd, currentFrame_, imageIndex, extent);
                vkCmdEndRenderPass(cmd);
            },
            std::move(lightUsages));

        // ---- 透明叠加 Pass（光照之后、SSR/合成之前）：BLEND/加性自发光物体
        //      深度只读测试（沿用 GBuffer 深度，透明体被不透明几何正确遮挡），
        //      管线混合叠加到离屏 HDR 颜色上（颜色附件 LOAD，不清除）----
        if (recordTransparent)
        {
            frameGraph_.AddPass(
                "transparent",
                [&]
                {
                    std::array<VkClearValue, 2> transparentClears{};
                    transparentClears[0].color = {0.0f, 0.0f, 0.0f, 0.0f}; // loadOp=LOAD，清除值不生效
                    transparentClears[1].depthStencil = {1.0f, 0};

                    VkRenderPassBeginInfo tPassInfo{};
                    tPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    tPassInfo.renderPass = transparentRenderPass_;
                    tPassInfo.framebuffer = transparentFramebuffers_[imageIndex];
                    tPassInfo.renderArea.offset = {0, 0};
                    tPassInfo.renderArea.extent = extent;
                    tPassInfo.clearValueCount = static_cast<uint32_t>(transparentClears.size());
                    tPassInfo.pClearValues = transparentClears.data();

                    vkCmdBeginRenderPass(cmd, &tPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                    recordTransparent(cmd, currentFrame_, imageIndex, extent);
                    vkCmdEndRenderPass(cmd);
                },
                {
                    {offscreenColorImage_->Get(), RGUsage::ColorAttachment,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                    {gDepthImages_[imageIndex].Get(), RGUsage::DepthTestRead},
                });
        }

        // ---- SSR Pass（光照之后、合成之前）----
        // 反射图/模糊图显式声明为颜色附件写入（ray/blur 渲染通道 finalLayout=SHADER_READ_ONLY）：
        // 渲染图在 ssr pass 前插入 UNDEFINED→COLOR_ATTACHMENT 转换（别名组首用屏障源并集，
        // 覆写上一帧 gDepth/反射图的残留访问），pass 内 ray→blur→composite 的采样布局由
        // endLayout=SHADER_READ_ONLY 衔接
        if (ssrEnabled_ && ssr_.IsValid())
        {
            frameGraph_.AddPass("ssr",
                                [&]
                                {
                                    ssr_.RecordPass(cmd, GBufferPositionView(imageIndex), GBufferNormalView(imageIndex),
                                                    offscreenColorImage_->View(), ssrViewProj_, ssrCameraPos_);
                                },
                                {
                                    {gPositionImages_[imageIndex].Get(), RGUsage::SampledRead},
                                    {gNormalImages_[imageIndex].Get(), RGUsage::SampledRead},
                                    {offscreenColorImage_->Get(), RGUsage::SampledRead},
                                    {ssr_.GetReflectionImage(), RGUsage::ColorAttachment,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                    {ssr_.GetBlurImage(), RGUsage::ColorAttachment,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                });
        }

        // ---- 合成 Pass：离屏颜色 + SSR 反射 → 交换链 ----
        // 更新合成描述符：绑定离屏颜色 + 反射（SSR 关闭时用 dummy white 作为黑色回退）
        {
            VkDescriptorImageInfo colorInfo{compositeSampler_, offscreenColorImage_->View(),
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkImageView reflView = (ssrEnabled_ && ssr_.IsValid()) ? ssr_.GetReflectionView() : dummyWhiteImage_.View();
            VkDescriptorImageInfo reflInfo{compositeSampler_, reflView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            std::array<VkWriteDescriptorSet, 2> writes{};
            for (uint32_t b = 0; b < 2; ++b)
            {
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstSet = compositeSet_;
                writes[b].dstBinding = b;
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[b].descriptorCount = 1;
            }
            writes[0].pImageInfo = &colorInfo;
            writes[1].pImageInfo = &reflInfo;
            vkUpdateDescriptorSets(ctx_.Device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

        const VkImage reflSampled =
            (ssrEnabled_ && ssr_.IsValid()) ? ssr_.GetReflectionImage() : dummyWhiteImage_.Get();
        frameGraph_.AddPass("composite",
                            [&]
                            {
                                std::array<VkClearValue, 1> compClears{};
                                compClears[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
                                VkRenderPassBeginInfo cPassInfo{};
                                cPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                                cPassInfo.renderPass = compositeRenderPass_;
                                cPassInfo.framebuffer = compositeFramebuffers_[imageIndex];
                                cPassInfo.renderArea.offset = {0, 0};
                                cPassInfo.renderArea.extent = extent;
                                cPassInfo.clearValueCount = 1;
                                cPassInfo.pClearValues = compClears.data();

                                vkCmdBeginRenderPass(cmd, &cPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                                compositePipeline_->Bind(cmd);
                                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                        compositePipeline_->GetLayout(), 0, 1, &compositeSet_, 0,
                                                        nullptr);
                                struct CompositePush
                                {
                                    float ssrStrength;
                                    float pad0;
                                    float pad1;
                                    float pad2;
                                };
                                CompositePush cpc{ssrEnabled_ ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
                                vkCmdPushConstants(cmd, compositePipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT,
                                                   0, sizeof(CompositePush), &cpc);
                                vkCmdDraw(cmd, 3, 1, 0, 0);
                                vkCmdEndRenderPass(cmd);
                                if (gpuProfiler_)
                                    gpuProfiler_->Write(cmd, currentFrame_, 2);
                            },
                            {
                                {offscreenColorImage_->Get(), RGUsage::SampledRead},
                                {reflSampled, RGUsage::SampledRead},
                                {swapImage, RGUsage::PresentSrc, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                            });
    }

    // UI覆盖层通道
    if (recordUi)
    {
        frameGraph_.AddPass("ui",
                            [&]
                            {
                                recordUi(cmd, imageIndex, extent);
                                if (gpuProfiler_)
                                    gpuProfiler_->Write(cmd, currentFrame_, 3);
                            },
                            {
                                {swapImage, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                            });
    }
    // 构建并执行渲染图：按 pass 依赖自动插入跨 pass 布局转换/同步 barrier
    // （profiler 时间戳必须记录在 pass lambda 内部，lambda 由 Execute 展开，
    //   写在 pass 声明处会导致时间戳全部落在 Execute 之前、测得时长为负）
    frameGraph_.Build();
    logTransientMemoryReport(frameGraph_);
    frameGraph_.Execute(cmd);

    // 无 UI 通道时在此补齐 UI 段结束时间戳（此时已位于全部 pass 之后）
    if (!recordUi && gpuProfiler_)
        gpuProfiler_->Write(cmd, currentFrame_, 3);

    VK_CHECK(vkEndCommandBuffer(cmd), "结束录制命令缓冲");

    // ---- 提交 ----
    const VkSemaphore waitSemaphore = imageAvailableSemaphores_[currentFrame_];
    const VkSemaphore signalSemaphore = renderFinishedSemaphores_[imageIndex];
    if (waitSemaphore == VK_NULL_HANDLE || signalSemaphore == VK_NULL_HANDLE)
        throw std::runtime_error("DrawFrame: 帧同步信号量为空（创建失败或重建时序异常）");

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    std::vector<VkCommandBuffer> submitBuffers;
    if (parallelPrePass)
    {
        // 只提交本帧实际录制的并行缓冲；未录制的处于 INITIAL 态，提交即验证层违规
        std::vector<VkCommandBuffer> parallelBuffers = parallelRecorder_.RecordedBuffers(currentFrame_);
        submitBuffers.insert(submitBuffers.end(), parallelBuffers.begin(), parallelBuffers.end());
    }
    submitBuffers.push_back(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = static_cast<uint32_t>(submitBuffers.size());
    submitInfo.pCommandBuffers = submitBuffers.data();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSemaphore;
    VK_CHECK(vkQueueSubmit(ctx_.GraphicsQueue(), 1, &submitInfo, inFlightFence), "提交帧命令");

    // ---- 呈现 ----
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSemaphore;
    presentInfo.swapchainCount = 1;
    VkSwapchainKHR swapchainHandle = swapchain_.Handle();
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(ctx_.PresentQueue(), &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR ||
        window_->ConsumeResizedFlag())
    {
        handleResize();
    }
    else if (presentResult != VK_SUCCESS)
    {
        VK_CHECK(presentResult, "呈现图像");
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFrames;
}
// 输出本帧渲染图的 transient 资源内存报告（生命周期区间 + 别名槽位 + 理论节省）。
// 仅首次构建打印一次，供开发者评估"生命周期不重叠资源共享显存"的优化潜力。
void Renderer::logTransientMemoryReport(const Render::RenderGraph& graph)
{
    static bool sReported = false;
    if (sReported)
        return;
    sReported = true;

    const std::vector<int32_t> slots = graph.PlanTransientSlots();
    VkDeviceSize independentTotal = 0;
    VkDeviceSize slotPeak = 0;
    std::vector<VkDeviceSize> slotBytes;
    for (uint32_t i = 0; i < graph.ImageCount(); ++i)
    {
        const VkDeviceSize size = graph.ImageSizeBytes(i);
        if (size == 0)
            continue;
        independentTotal += size;
        const int32_t slot = slots[i];
        if (slot < 0)
            continue;
        if (slotBytes.size() <= static_cast<size_t>(slot))
            slotBytes.resize(static_cast<size_t>(slot) + 1, 0);
        slotBytes[slot] = std::max(slotBytes[slot], size);
        const Render::RGLifetime life = graph.ResourceLifetime(i);
        LOG_INFO("[RenderGraph] resource='" << graph.ImageName(i) << "' life=[" << life.firstUse << ',' << life.lastUse
                                            << "] size=" << size << "B slot=" << slot);
    }
    for (VkDeviceSize b : slotBytes)
        slotPeak += b;
    const double saving = independentTotal > 0
                              ? (1.0 - static_cast<double>(slotPeak) / static_cast<double>(independentTotal)) * 100.0
                              : 0.0;
    LOG_INFO("[RenderGraph] transient 内存报告：独立分配=" << independentTotal << "B，别名槽位峰值=" << slotPeak
                                                           << "B，理论节省=" << saving << "%");
}
} // namespace BigHero
