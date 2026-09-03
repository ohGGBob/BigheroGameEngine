#include "render/Renderer.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "core/VkUtils.h"
#include "platform/Window.h"
#include "render/Context.h"
#include "render/image.h"
#include "render/pipeline.h"
#include "render/shader_loader.h"

#include <array>
#include <memory>
#include <stdexcept>

namespace BigHero
{
Renderer::Renderer(const Context& ctx, Window& window)
    : ctx_(ctx), window_(window), depthFormat_(pickDepthFormat()), sampleCount_(pickSampleCount())
{
    swapchain_.Create(ctx_, window_);
    renderPass_.Create(ctx_.Device(), swapchain_.Format(), depthFormat_, sampleCount_);
    // 延迟渲染：几何通道（GBuffer）与光照通道始终创建，供管线在启动时构建；
    // GBuffer 图像与帧缓冲仅在启用延迟模式时创建
    createDeferredRenderPass();
    createLightingRenderPass();
    createFrameResources();
    createCommandResources();
    createDummyWhiteImage();
    createSyncObjects();

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

    LOG_INFO("渲染器初始化完成，帧并行数: " << kMaxFrames << "，MSAA采样数: " << static_cast<uint32_t>(sampleCount_)
                                            << "x");
}

Renderer::~Renderer()
{
    ctx_.WaitIdle();
    ssao_.Destroy();
    destroyDeferredResources();
    destroyDeferredRenderPass();
    destroyLightingRenderPass();
    destroySyncObjects();
    destroyFrameResources();
    parallelRecorder_.Destroy();
    if (commandPool_ != VK_NULL_HANDLE)
        vkDestroyCommandPool(ctx_.Device(), commandPool_, nullptr);
    renderPass_.Release();
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

void Renderer::createFrameResources()
{
    const VkExtent2D extent = swapchain_.Extent();
    const uint32_t imageCount = swapchain_.ImageCount();

    // MSAA中间图像：颜色解析源 + 深度，整条交换链共用
    msaaColorImage_.Destroy();
    msaaDepthImage_.Destroy();
    if (sampleCount_ != VK_SAMPLE_COUNT_1_BIT)
    {
        msaaColorImage_.Create(ctx_, extent.width, extent.height, swapchain_.Format(),
                               VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, sampleCount_);
        msaaDepthImage_.Create(ctx_, extent.width, extent.height, depthFormat_,
                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, sampleCount_);
    }

    framebuffers_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        VkImageView attachments[3]{};
        uint32_t attachmentCount = 0;
        if (sampleCount_ != VK_SAMPLE_COUNT_1_BIT)
        {
            attachments[attachmentCount++] = msaaColorImage_.View();
            attachments[attachmentCount++] = msaaDepthImage_.View();
        }
        attachments[attachmentCount++] = swapchain_.Views()[i];

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass_.renderPass;
        fbInfo.attachmentCount = attachmentCount;
        fbInfo.pAttachments = attachments;
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(ctx_.Device(), &fbInfo, nullptr, &framebuffers_[i]), "创建帧缓冲");
    }
}

void Renderer::destroyFrameResources()
{
    for (VkFramebuffer fb : framebuffers_)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(ctx_.Device(), fb, nullptr);
    framebuffers_.clear();
    msaaColorImage_.Destroy();
    msaaDepthImage_.Destroy();
}

void Renderer::createSyncObjects()
{
    imageAvailableSemaphores_.resize(kMaxFrames);
    inFlightFences_.resize(kMaxFrames);
    renderFinishedSemaphores_.resize(swapchain_.ImageCount());

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFrames; ++i)
    {
        VK_CHECK(vkCreateSemaphore(ctx_.Device(), &semInfo, nullptr, &imageAvailableSemaphores_[i]),
                 "创建图像获取信号量");
        VK_CHECK(vkCreateFence(ctx_.Device(), &fenceInfo, nullptr, &inFlightFences_[i]), "创建帧栅栏");
    }
    for (size_t i = 0; i < renderFinishedSemaphores_.size(); ++i)
    {
        VK_CHECK(vkCreateSemaphore(ctx_.Device(), &semInfo, nullptr, &renderFinishedSemaphores_[i]),
                 "创建渲染完成信号量");
    }
}

void Renderer::destroySyncObjects()
{
    for (VkSemaphore sem : imageAvailableSemaphores_)
        if (sem != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx_.Device(), sem, nullptr);
    for (VkSemaphore sem : renderFinishedSemaphores_)
        if (sem != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx_.Device(), sem, nullptr);
    for (VkFence fence : inFlightFences_)
        if (fence != VK_NULL_HANDLE)
            vkDestroyFence(ctx_.Device(), fence, nullptr);
    imageAvailableSemaphores_.clear();
    renderFinishedSemaphores_.clear();
    inFlightFences_.clear();
}

void Renderer::handleResize()
{
    ctx_.WaitIdle();
    destroyFrameResources();
    destroySyncObjects();

    // 借助oldSwapchain创建新交换链，随后释放旧资源
    Swapchain fresh;
    fresh.Create(ctx_, window_, swapchain_.Handle());
    const bool formatChanged = fresh.Format() != swapchain_.Format();
    swapchain_.Destroy();
    swapchain_ = std::move(fresh);

    if (formatChanged)
    {
        LOG_WARN("交换链格式发生变化，重建渲染通道");
        depthFormat_ = pickDepthFormat();
        renderPass_.Release();
        renderPass_.Create(ctx_.Device(), swapchain_.Format(), depthFormat_, sampleCount_);
        if (renderPassRecreateCallback_)
            renderPassRecreateCallback_();
    }

    createFrameResources();
    createSyncObjects();
    LOG_INFO("交换链已重建: " << swapchain_.Extent().width << "x" << swapchain_.Extent().height);

    // 延迟渲染：格式变化时重建几何/光照通道（否则管线引用过期通道）
    if (formatChanged)
    {
        destroyDeferredRenderPass();
        destroyLightingRenderPass();
        createDeferredRenderPass();
        createLightingRenderPass();
        if (renderPassRecreateCallback_)
            renderPassRecreateCallback_();
    }
    if (deferredEnabled_)
    {
        destroyDeferredFramebuffers();
        createDeferredFramebuffers();
    }

    // SSAO：尺寸变化时重建
    if (ssaoEnabled_)
        ssao_.Recreate(ctx_, swapchain_.Extent());

    // SSR：尺寸变化时重建
    if (ssrEnabled_)
        ssr_.Recreate(ctx_, swapchain_.Extent());

    // 后处理：尺寸变化时重建离屏缓冲与帧缓冲；格式变化时完全重建
    if (postProcessEnabled_)
    {
        destroyOffscreenFramebuffer();
        if (formatChanged)
        {
            postProcessor_.Destroy();
            postProcessor_.Init(ctx_, swapchain_.Extent(), swapchain_.Format(), sampleCount_, swapchain_.Views());
        }
        else
        {
            postProcessor_.Recreate(ctx_, swapchain_.Extent(), swapchain_.Views());
        }
        createOffscreenFramebuffer();
    }

    if (resizeCallback_)
        resizeCallback_();
}

void Renderer::createDeferredRenderPass()
{
    if (deferredRenderPass_ != VK_NULL_HANDLE)
        return;
    const Render::GBufferFormats fmt = Render::DefaultGBufferFormats();

    // 几何通道：4 附件（3 GBuffer + 深度），单子通道，GBuffer 最终 SHADER_READ_ONLY
    std::array<VkAttachmentDescription, 4> atts{};
    atts[0].format = fmt.albedo;
    atts[1].format = fmt.normal;
    atts[2].format = fmt.position;
    for (uint32_t i = 0; i < 3; ++i)
    {
        atts[i].samples = VK_SAMPLE_COUNT_1_BIT;
        atts[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        atts[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        atts[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    // 深度附件
    atts[3].format = depthFormat_;
    atts[3].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[3].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    atts[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const VkAttachmentReference colorRefs[3] = {{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                                {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    const VkAttachmentReference depthRef{3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 3;
    sub.pColorAttachments = colorRefs;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = static_cast<uint32_t>(atts.size());
    passInfo.pAttachments = atts.data();
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &sub;
    passInfo.dependencyCount = 1;
    passInfo.pDependencies = &dep;

    VK_CHECK(vkCreateRenderPass(ctx_.Device(), &passInfo, nullptr, &deferredRenderPass_), "创建延迟几何渲染通道");
}

void Renderer::destroyDeferredRenderPass()
{
    if (deferredRenderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx_.Device(), deferredRenderPass_, nullptr);
        deferredRenderPass_ = VK_NULL_HANDLE;
    }
}

void Renderer::createLightingRenderPass()
{
    if (lightingRenderPass_ != VK_NULL_HANDLE)
        return;

    // 光照 Pass 输出到离屏 HDR 颜色缓冲（RGBA16F），最终布局 SHADER_READ_ONLY 供 SSR/合成采样
    VkAttachmentDescription att{};
    att.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &att;
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    info.dependencyCount = 1;
    info.pDependencies = &dep;

    VK_CHECK(vkCreateRenderPass(ctx_.Device(), &info, nullptr, &lightingRenderPass_), "创建延迟光照渲染通道");
}

void Renderer::destroyLightingRenderPass()
{
    if (lightingRenderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx_.Device(), lightingRenderPass_, nullptr);
        lightingRenderPass_ = VK_NULL_HANDLE;
    }
}

void Renderer::createDeferredFramebuffers()
{
    const VkExtent2D extent = swapchain_.Extent();
    const uint32_t imageCount = swapchain_.ImageCount();
    const Render::GBufferFormats fmt = Render::DefaultGBufferFormats();

    gAlbedoImages_.resize(imageCount);
    gNormalImages_.resize(imageCount);
    gPositionImages_.resize(imageCount);
    gDepthImages_.resize(imageCount);
    deferredFramebuffers_.resize(imageCount);
    lightingFramebuffers_.resize(imageCount);

    for (uint32_t i = 0; i < imageCount; ++i)
    {
        // GBuffer 图像：颜色附件 + 可采样（SSAO/光照 Pass 纹理采样）
        gAlbedoImages_[i].Create(ctx_, extent.width, extent.height, fmt.albedo,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        gNormalImages_[i].Create(ctx_, extent.width, extent.height, fmt.normal,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        gPositionImages_[i].Create(ctx_, extent.width, extent.height, fmt.position,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        gDepthImages_[i].Create(ctx_, extent.width, extent.height, depthFormat_,
                                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                VK_IMAGE_ASPECT_DEPTH_BIT);

        // 几何通道帧缓冲：3 GBuffer + 深度
        VkImageView views[4] = {gAlbedoImages_[i].View(), gNormalImages_[i].View(), gPositionImages_[i].View(),
                                gDepthImages_[i].View()};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = deferredRenderPass_;
        fbInfo.attachmentCount = 4;
        fbInfo.pAttachments = views;
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(ctx_.Device(), &fbInfo, nullptr, &deferredFramebuffers_[i]), "创建延迟几何帧缓冲");

        // 光照通道帧缓冲：离屏 HDR 颜色缓冲（非交换链）
        if (!offscreenColorImage_)
        {
            offscreenColorImage_ = std::make_unique<Image>();
            offscreenColorImage_->Create(ctx_, extent.width, extent.height, VK_FORMAT_R16G16B16A16_SFLOAT,
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        }
        VkImageView offscreenView = offscreenColorImage_->View();
        VkFramebufferCreateInfo lightFb{};
        lightFb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        lightFb.renderPass = lightingRenderPass_;
        lightFb.attachmentCount = 1;
        lightFb.pAttachments = &offscreenView;
        lightFb.width = extent.width;
        lightFb.height = extent.height;
        lightFb.layers = 1;
        VK_CHECK(vkCreateFramebuffer(ctx_.Device(), &lightFb, nullptr, &lightingFramebuffers_[i]),
                 "创建延迟光照帧缓冲");
    }

    // 合成通道帧缓冲：绑定到交换链图像
    createCompositeResources();
}

void Renderer::destroyDeferredFramebuffers()
{
    destroyCompositeResources();
    for (VkFramebuffer fb : deferredFramebuffers_)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(ctx_.Device(), fb, nullptr);
    deferredFramebuffers_.clear();
    for (VkFramebuffer fb : lightingFramebuffers_)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(ctx_.Device(), fb, nullptr);
    lightingFramebuffers_.clear();
    for (Image& img : gAlbedoImages_)
        img.Destroy();
    for (Image& img : gNormalImages_)
        img.Destroy();
    for (Image& img : gPositionImages_)
        img.Destroy();
    for (Image& img : gDepthImages_)
        img.Destroy();
    gAlbedoImages_.clear();
    gNormalImages_.clear();
    gPositionImages_.clear();
    gDepthImages_.clear();
    offscreenColorImage_.reset();
}

void Renderer::SetDeferred(bool enabled)
{
    if (enabled == deferredEnabled_)
        return;
    deferredEnabled_ = enabled;
    if (enabled)
    {
        createDeferredResources();
        LOG_INFO("延迟渲染已启用（GBuffer MRT + 纹理采样延迟光照）");
    }
    else
    {
        // 关闭延迟时自动关闭 SSAO（仅延迟模式支持）
        if (ssaoEnabled_)
        {
            ssaoEnabled_ = false;
            ssao_.Destroy();
            LOG_INFO("SSAO 已随延迟渲染关闭");
        }
        // 关闭延迟时自动关闭 SSR（仅延迟模式支持）
        if (ssrEnabled_)
        {
            ssrEnabled_ = false;
            ssr_.Destroy();
            LOG_INFO("SSR 已随延迟渲染关闭");
        }
        destroyDeferredResources();
        LOG_INFO("延迟渲染已关闭，回退前向渲染");
    }
}

void Renderer::createDeferredResources()
{
    createDeferredRenderPass();
    createLightingRenderPass();
    createDeferredFramebuffers();
}

void Renderer::destroyDeferredResources()
{
    // 仅释放 GBuffer 帧缓冲与图像；渲染通道本身始终保留（与交换链格式同步，
    // 供 GBuffer/光照管线持续引用），避免开关延迟模式后管线引用到已销毁的渲染通道。
    destroyDeferredFramebuffers();
}

void Renderer::SetSSAO(bool enabled)
{
    if (enabled == ssaoEnabled_)
        return;
    if (enabled && !deferredEnabled_)
    {
        LOG_WARN("SSAO 仅支持延迟渲染模式，已忽略");
        return;
    }
    ssaoEnabled_ = enabled;
    if (enabled)
    {
        ssao_.Init(ctx_, swapchain_.Extent());
        LOG_INFO("SSAO 已启用（半径 " << ssao_.radius << "，强度 " << ssao_.strength << "）");
    }
    else
    {
        ctx_.WaitIdle();
        ssao_.Destroy();
        LOG_INFO("SSAO 已关闭");
    }
}

void Renderer::SetSSR(bool enabled)
{
    if (enabled == ssrEnabled_)
        return;
    if (enabled && !deferredEnabled_)
    {
        LOG_WARN("SSR 仅支持延迟渲染模式，已忽略");
        return;
    }
    ssrEnabled_ = enabled;
    if (enabled)
    {
        ssr_.Init(ctx_, swapchain_.Extent());
        LOG_INFO("SSR 已启用（最大距离 " << ssr_.maxDistance << "，步数 " << ssr_.stepCount << "）");
    }
    else
    {
        ctx_.WaitIdle();
        ssr_.Destroy();
        LOG_INFO("SSR 已关闭");
    }
}

void Renderer::SetPostProcessing(bool enabled)
{
    if (enabled == postProcessEnabled_)
        return;
    if (enabled && deferredEnabled_)
    {
        LOG_WARN("后处理暂不支持延迟渲染模式，已忽略");
        return;
    }
    postProcessEnabled_ = enabled;
    if (enabled)
    {
        postProcessor_.Init(ctx_, swapchain_.Extent(), swapchain_.Format(), sampleCount_, swapchain_.Views());
        createOffscreenFramebuffer();
        LOG_INFO("后处理已启用（Bloom + ACES 色调映射）");
    }
    else
    {
        vkDeviceWaitIdle(ctx_.Device());
        destroyOffscreenFramebuffer();
        postProcessor_.Destroy();
        LOG_INFO("后处理已关闭");
    }
}

void Renderer::createOffscreenFramebuffer()
{
    destroyOffscreenFramebuffer();
    const VkExtent2D extent = swapchain_.Extent();
    const bool useMsaa = sampleCount_ != VK_SAMPLE_COUNT_1_BIT;

    VkImageView attachments[3]{};
    uint32_t count = 0;
    if (useMsaa)
    {
        attachments[count++] = postProcessor_.OffscreenMsaaColorView();
        attachments[count++] = msaaDepthImage_.View();
        attachments[count++] = postProcessor_.OffscreenResolveView();
        // 升级 22：把 MSAA 深度图交给后处理，供景深还原线性深度
        postProcessor_.SetSceneDepth(msaaDepthImage_.View(), msaaDepthImage_.Get(), true);
    }
    else
    {
        attachments[count++] = postProcessor_.OffscreenResolveView();
    }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_.renderPass;
    fbInfo.attachmentCount = count;
    fbInfo.pAttachments = attachments;
    fbInfo.width = extent.width;
    fbInfo.height = extent.height;
    fbInfo.layers = 1;
    VK_CHECK(vkCreateFramebuffer(ctx_.Device(), &fbInfo, nullptr, &offscreenFramebuffer_), "创建离屏帧缓冲");
}

void Renderer::destroyOffscreenFramebuffer()
{
    if (offscreenFramebuffer_ != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(ctx_.Device(), offscreenFramebuffer_, nullptr);
        offscreenFramebuffer_ = VK_NULL_HANDLE;
    }
}

void Renderer::createCompositeResources()
{
    const VkExtent2D extent = swapchain_.Extent();
    const uint32_t imageCount = swapchain_.ImageCount();

    // 合成渲染通道：输出到交换链，finalLayout PRESENT_SRC
    if (compositeRenderPass_ == VK_NULL_HANDLE)
    {
        VkAttachmentDescription att{};
        att.format = swapchain_.Format();
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dep.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &att;
        info.subpassCount = 1;
        info.pSubpasses = &sub;
        info.dependencyCount = 1;
        info.pDependencies = &dep;
        VK_CHECK(vkCreateRenderPass(ctx_.Device(), &info, nullptr, &compositeRenderPass_), "创建合成渲染通道");
    }

    // 合成帧缓冲：绑定交换链图像
    compositeFramebuffers_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i)
    {
        VkImageView swapView = swapchain_.Views()[i];
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = compositeRenderPass_;
        fb.attachmentCount = 1;
        fb.pAttachments = &swapView;
        fb.width = extent.width;
        fb.height = extent.height;
        fb.layers = 1;
        VK_CHECK(vkCreateFramebuffer(ctx_.Device(), &fb, nullptr, &compositeFramebuffers_[i]), "创建合成帧缓冲");
    }

    // 采样器
    if (compositeSampler_ == VK_NULL_HANDLE)
    {
        VkSamplerCreateInfo samp{};
        samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samp.magFilter = VK_FILTER_LINEAR;
        samp.minFilter = VK_FILTER_LINEAR;
        samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samp.maxLod = VK_LOD_CLAMP_NONE;
        VK_CHECK(vkCreateSampler(ctx_.Device(), &samp, nullptr, &compositeSampler_), "创建合成采样器");
    }

    // 描述符布局：binding0=sceneColor, binding1=reflection
    if (compositeLayout_ == VK_NULL_HANDLE)
    {
        std::array<VkDescriptorSetLayoutBinding, 2> binds{};
        for (uint32_t b = 0; b < 2; ++b)
        {
            binds[b].binding = b;
            binds[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binds[b].descriptorCount = 1;
            binds[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            binds[b].pImmutableSamplers = &compositeSampler_;
        }
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 2;
        info.pBindings = binds.data();
        VK_CHECK(vkCreateDescriptorSetLayout(ctx_.Device(), &info, nullptr, &compositeLayout_), "创建合成描述符布局");
    }

    // 描述符池
    if (compositeDescPool_ == VK_NULL_HANDLE)
    {
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &poolSize;
        pool.maxSets = 1;
        VK_CHECK(vkCreateDescriptorPool(ctx_.Device(), &pool, nullptr, &compositeDescPool_), "创建合成描述符池");

        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = compositeDescPool_;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &compositeLayout_;
        VK_CHECK(vkAllocateDescriptorSets(ctx_.Device(), &alloc, &compositeSet_), "分配合成描述符集");
    }

    // 合成管线
    if (!compositePipeline_)
    {
        Render::GraphicsPipelineConfig cfg;
        cfg.vertexBindings = {};
        cfg.vertexAttributes = {};
        cfg.cullMode = VK_CULL_MODE_NONE;
        cfg.depthTest = false;
        cfg.depthWrite = false;
        cfg.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
        cfg.colorAttachmentCount = 1;
        cfg.setLayouts = {compositeLayout_};
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16}};

        const auto fullscreenSpv = Render::ReadShaderFile("shaders/pp_fullscreen.vert.spv");
        Render::ShaderModuleHandle v(ctx_.Device(), fullscreenSpv);
        Render::ShaderModuleHandle f(ctx_.Device(), Render::ReadShaderFile("shaders/deferred_composite.frag.spv"));
        compositePipeline_ = std::make_unique<Render::GraphicsPipeline>(ctx_.Device(), compositeRenderPass_,
                                                                        std::move(v), std::move(f), cfg);
    }
}

void Renderer::destroyCompositeResources()
{
    compositePipeline_.reset();
    for (VkFramebuffer fb : compositeFramebuffers_)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(ctx_.Device(), fb, nullptr);
    compositeFramebuffers_.clear();
    if (compositeRenderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx_.Device(), compositeRenderPass_, nullptr);
        compositeRenderPass_ = VK_NULL_HANDLE;
    }
    if (compositeDescPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(ctx_.Device(), compositeDescPool_, nullptr);
        compositeDescPool_ = VK_NULL_HANDLE;
    }
    if (compositeLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(ctx_.Device(), compositeLayout_, nullptr);
        compositeLayout_ = VK_NULL_HANDLE;
    }
    if (compositeSampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(ctx_.Device(), compositeSampler_, nullptr);
        compositeSampler_ = VK_NULL_HANDLE;
    }
    compositeSet_ = VK_NULL_HANDLE;
}

VkImageView Renderer::GBufferAlbedoView(uint32_t imageIndex) const noexcept
{
    return (imageIndex < gAlbedoImages_.size()) ? gAlbedoImages_[imageIndex].View() : VK_NULL_HANDLE;
}
VkImageView Renderer::GBufferNormalView(uint32_t imageIndex) const noexcept
{
    return (imageIndex < gNormalImages_.size()) ? gNormalImages_[imageIndex].View() : VK_NULL_HANDLE;
}
VkImageView Renderer::GBufferPositionView(uint32_t imageIndex) const noexcept
{
    return (imageIndex < gPositionImages_.size()) ? gPositionImages_[imageIndex].View() : VK_NULL_HANDLE;
}

void Renderer::DrawFrame(const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordScene,
                         const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordUi,
                         const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& prePass,
                         const std::function<void(VkCommandBuffer, uint32_t, uint32_t, VkExtent2D)>& recordLighting,
                         const std::function<void(Render::ParallelCommandRecorder&, uint32_t)>& parallelPrePass)
{
    // 窗口最小化时挂起等待，直到恢复有效尺寸
    while (true)
    {
        const auto [width, height] = window_.GetFramebufferSize();
        if (width > 0 && height > 0)
            break;
        window_.WaitEvents();
    }

    if (window_.ConsumeResizedFlag())
        handleResize();

    const VkDevice device = ctx_.Device();
    const VkFence inFlightFence = inFlightFences_[currentFrame_];
    VK_CHECK(vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX), "等待帧栅栏");

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
    if (ssrEnabled_ && ssr_.IsValid())
        frameGraph_.RegisterImage("ssrReflection", ssr_.GetReflectionImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // 1) 深度预通道（阴影贴图等）——黑盒 pass：内部布局自洽，渲染图不声明其资源
    frameGraph_.AddPass("shadow", [&]
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
            frameGraph_.AddPass("scene", [&]
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
                                    {msaaDepthImage_.Get(), RGUsage::DepthAttachment,
                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
                                });

            // 后处理链（黑盒：内部 DoF/MB/Bloom 自洽）：输入场景颜色+深度，输出交换链
            frameGraph_.AddPass("post", [&]
                                {
                                    postProcessor_.RecordBloom(cmd, imageIndex, extent, postProcessNear_, postProcessFar_);
                                },
                                {
                                    {postProcessor_.OffscreenResolveImage(), RGUsage::SampledRead},
                                    {msaaDepthImage_.Get(), RGUsage::DepthReadOnly},
                                    {swapImage, RGUsage::PresentSrc, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                });
        }
        else
        {
            frameGraph_.AddPass("scene", [&]
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
                                },
                                {
                                    {swapImage, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                    {msaaDepthImage_.Get(), RGUsage::DepthAttachment,
                                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
                                });
        }

        // 场景+后处理结束（= UI 开始）
        if (gpuProfiler_)
            gpuProfiler_->Write(cmd, currentFrame_, 2);
    }

    // 延迟渲染：几何 Pass -> (SSAO) -> 光照 Pass -> (SSR) -> 合成 Pass
    if (deferredEnabled_)
    {
        if (gpuProfiler_)
            gpuProfiler_->Write(cmd, currentFrame_, 1);

        // ---- 几何 Pass：写 GBuffer ----
        frameGraph_.AddPass("gBuffer", [&]
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
                                {gAlbedoImages_[imageIndex].Get(), RGUsage::ColorAttachment,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                {gNormalImages_[imageIndex].Get(), RGUsage::ColorAttachment,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                {gPositionImages_[imageIndex].Get(), RGUsage::ColorAttachment,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                {gDepthImages_[imageIndex].Get(), RGUsage::DepthAttachment,
                                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL},
                            });

        // ---- SSAO Pass（几何之后、光照之前）----
        if (ssaoEnabled_ && ssao_.IsValid())
        {
            frameGraph_.AddPass("ssao", [&]
                                {
                                    ssao_.RecordPass(cmd, GBufferPositionView(imageIndex), GBufferNormalView(imageIndex),
                                                     ssaoViewProj_, ssaoCameraPos_);
                                },
                                {
                                    {gPositionImages_[imageIndex].Get(), RGUsage::SampledRead},
                                    {gNormalImages_[imageIndex].Get(), RGUsage::SampledRead},
                                    {ssao_.GetAOImage(), RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                });
        }

        // ---- 光照 Pass：采样 GBuffer (+AO) 输出到离屏 HDR 缓冲 ----
        if (gpuProfiler_)
            gpuProfiler_->Write(cmd, currentFrame_, 2);

        std::vector<RGUsageDecl> lightUsages;
        lightUsages.reserve(5);
        lightUsages.push_back({gAlbedoImages_[imageIndex].Get(), RGUsage::SampledRead});
        lightUsages.push_back({gNormalImages_[imageIndex].Get(), RGUsage::SampledRead});
        lightUsages.push_back({gPositionImages_[imageIndex].Get(), RGUsage::SampledRead});
        if (ssaoEnabled_ && ssao_.IsValid())
            lightUsages.push_back({ssao_.GetAOImage(), RGUsage::SampledRead});
        frameGraph_.AddPass("lighting", [&]
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

        // ---- SSR Pass（光照之后、合成之前）----
        if (ssrEnabled_ && ssr_.IsValid())
        {
            frameGraph_.AddPass("ssr", [&]
                                {
                                    ssr_.RecordPass(cmd, GBufferPositionView(imageIndex), GBufferNormalView(imageIndex),
                                                    offscreenColorImage_->View(), ssrViewProj_, ssrCameraPos_);
                                },
                                {
                                    {gPositionImages_[imageIndex].Get(), RGUsage::SampledRead},
                                    {gNormalImages_[imageIndex].Get(), RGUsage::SampledRead},
                                    {offscreenColorImage_->Get(), RGUsage::SampledRead},
                                });
        }

        // ---- 合成 Pass：离屏颜色 + SSR 反射 → 交换链 ----
        if (gpuProfiler_)
            gpuProfiler_->Write(cmd, currentFrame_, 3);

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
        frameGraph_.AddPass("composite", [&]
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
                                                        compositePipeline_->GetLayout(), 0, 1, &compositeSet_, 0, nullptr);
                                struct CompositePush
                                {
                                    float ssrStrength;
                                    float pad0;
                                    float pad1;
                                    float pad2;
                                };
                                CompositePush cpc{ssrEnabled_ ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
                                vkCmdPushConstants(cmd, compositePipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                                   sizeof(CompositePush), &cpc);
                                vkCmdDraw(cmd, 3, 1, 0, 0);
                                vkCmdEndRenderPass(cmd);
                            },
                            {
                                {offscreenColorImage_->Get(), RGUsage::SampledRead},
                                {reflSampled, RGUsage::SampledRead},
                                {swapImage, RGUsage::PresentSrc, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                            });
    }

    // UI覆盖层通道
    if (gpuProfiler_)
        gpuProfiler_->Write(cmd, currentFrame_, 2);
    if (recordUi)
    {
        frameGraph_.AddPass("ui", [&]
                            {
                                recordUi(cmd, imageIndex, extent);
                                if (gpuProfiler_)
                                    gpuProfiler_->Write(cmd, currentFrame_, 3);
                            },
                            {
                                {swapImage, RGUsage::ColorAttachment, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                            });
    }
    else if (gpuProfiler_)
    {
        gpuProfiler_->Write(cmd, currentFrame_, 3);
    }

    // 构建并执行渲染图：按 pass 依赖自动插入跨 pass 布局转换/同步 barrier
    frameGraph_.Build();
    frameGraph_.Execute(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd), "结束录制命令缓冲");

    // ---- 提交 ----
    const VkSemaphore waitSemaphore = imageAvailableSemaphores_[currentFrame_];
    const VkSemaphore signalSemaphore = renderFinishedSemaphores_[imageIndex];

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    std::vector<VkCommandBuffer> submitBuffers;
    if (parallelPrePass)
    {
        const auto& parallelBuffers = parallelRecorder_.Buffers(currentFrame_);
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
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || window_.ConsumeResizedFlag())
    {
        handleResize();
    }
    else if (presentResult != VK_SUCCESS)
    {
        VK_CHECK(presentResult, "呈现图像");
    }

    currentFrame_ = (currentFrame_ + 1) % kMaxFrames;
}
} // namespace BigHero
