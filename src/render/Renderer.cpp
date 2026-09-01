#include "render/Renderer.h"
#include "render/Context.h"
#include "platform/Window.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "core/VkUtils.h"

#include <array>
#include <memory>
#include <stdexcept>

namespace BigHero
{
    Renderer::Renderer(const Context& ctx, Window& window)
        : ctx_(ctx), window_(window)
    {
        swapchain_.Create(ctx_, window_);
        depthFormat_ = pickDepthFormat();
        sampleCount_ = pickSampleCount();
        renderPass_.Create(ctx_.Device(), swapchain_.Format(), depthFormat_, sampleCount_);
        // 延迟渲染通道（双子通道）始终创建，供 GBuffer/延迟光照管线在启动时构建；
        // GBuffer 图像与帧缓冲仅在启用延迟模式时创建
        createDeferredRenderPass();
        createFrameResources();
        createCommandResources();
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

        LOG_INFO("渲染器初始化完成，帧并行数: " << kMaxFrames
            << "，MSAA采样数: " << static_cast<uint32_t>(sampleCount_) << "x");
    }

    Renderer::~Renderer()
    {
        ctx_.WaitIdle();
        destroyDeferredResources();
        destroyDeferredRenderPass();
        destroySyncObjects();
        destroyFrameResources();
        if (commandPool_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(ctx_.Device(), commandPool_, nullptr);
        renderPass_.Release();
        swapchain_.Destroy();
    }

    VkFormat Renderer::pickDepthFormat() const
    {
        const VkFormat format = FindSupportedFormat(ctx_.PhysicalDevice(),
            {
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_FORMAT_D24_UNORM_S8_UINT,
                VK_FORMAT_D16_UNORM
            },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
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

        const VkSampleCountFlagBits candidates[] = {
            VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_2_BIT
        };
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
                VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
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
            VK_CHECK(vkCreateSemaphore(ctx_.Device(), &semInfo, nullptr, &imageAvailableSemaphores_[i]), "创建图像获取信号量");
            VK_CHECK(vkCreateFence(ctx_.Device(), &fenceInfo, nullptr, &inFlightFences_[i]), "创建帧栅栏");
        }
        for (size_t i = 0; i < renderFinishedSemaphores_.size(); ++i)
        {
            VK_CHECK(vkCreateSemaphore(ctx_.Device(), &semInfo, nullptr, &renderFinishedSemaphores_[i]), "创建渲染完成信号量");
        }
    }

    void Renderer::destroySyncObjects()
    {
        for (VkSemaphore sem : imageAvailableSemaphores_)
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(ctx_.Device(), sem, nullptr);
        for (VkSemaphore sem : renderFinishedSemaphores_)
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(ctx_.Device(), sem, nullptr);
        for (VkFence fence : inFlightFences_)
            if (fence != VK_NULL_HANDLE) vkDestroyFence(ctx_.Device(), fence, nullptr);
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
            // 表面格式变化时渲染通道需重建；此时已创建的图形管线将不再兼容，正常场景极少发生
            LOG_WARN("交换链格式发生变化，重建渲染通道");
            depthFormat_ = pickDepthFormat();
            renderPass_.Release();
            renderPass_.Create(ctx_.Device(), swapchain_.Format(), depthFormat_, sampleCount_);
            // 通知外部重建依赖该渲染通道的图形管线（场景/天空盒），否则后续绘制将崩溃
            if (renderPassRecreateCallback_)
                renderPassRecreateCallback_();
        }

        createFrameResources();
        createSyncObjects();
        LOG_INFO("交换链已重建: " << swapchain_.Extent().width << "x" << swapchain_.Extent().height);

        // 延迟渲染：渲染通道附件含交换链格式，格式变化时必须同步重建（与开关状态无关，
        // 否则后续启用延迟模式时 GBuffer 管线会引用过期渲染通道）。GBuffer 图像/帧缓冲仅启用时存在。
        if (formatChanged)
        {
            destroyDeferredRenderPass();
            createDeferredRenderPass();
            if (renderPassRecreateCallback_)
                renderPassRecreateCallback_(); // 重建依赖该渲染通道的管线（含 GBuffer/光照管线）
        }
        if (deferredEnabled_)
        {
            destroyDeferredFramebuffers();
            createDeferredFramebuffers();
        }

        if (resizeCallback_)
            resizeCallback_();
    }

    void Renderer::createDeferredRenderPass()
    {
        if (deferredRenderPass_ != VK_NULL_HANDLE)
            return;
        const Render::GBufferFormats fmt = Render::DefaultGBufferFormats();

        std::array<VkAttachmentDescription, 5> atts{};
        // 0..2 GBuffer 颜色附件（MRT）
        for (uint32_t i = 0; i < 3; ++i)
        {
            atts[i].format = (i == 0) ? fmt.albedo : fmt.normal;
            atts[i].samples = VK_SAMPLE_COUNT_1_BIT;
            atts[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            atts[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            atts[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            atts[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            atts[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            atts[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        atts[0].format = fmt.albedo;
        atts[1].format = fmt.normal;
        atts[2].format = fmt.position;
        // 3 深度附件（几何子通道使用）
        atts[3].format = depthFormat_;
        atts[3].samples = VK_SAMPLE_COUNT_1_BIT;
        atts[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[3].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        atts[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        // 4 交换链颜色附件（延迟光照子通道输出）
        atts[4].format = swapchain_.Format();
        atts[4].samples = VK_SAMPLE_COUNT_1_BIT;
        atts[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        atts[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        atts[4].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        const VkAttachmentReference colorRefs[3] = {
            { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
            { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
            { 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
        };
        const VkAttachmentReference depthRef{ 3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        const VkAttachmentReference inputRefs[3] = {
            { 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            { 2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
        };
        const VkAttachmentReference outColorRef{ 4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpasses[2]{};
        // 子通道 0：几何，写 GBuffer + 深度
        subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpasses[0].colorAttachmentCount = 3;
        subpasses[0].pColorAttachments = colorRefs;
        subpasses[0].pDepthStencilAttachment = &depthRef;
        // 子通道 1：延迟光照，读 GBuffer 输入附件，写交换链颜色
        subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpasses[1].colorAttachmentCount = 1;
        subpasses[1].pColorAttachments = &outColorRef;
        subpasses[1].inputAttachmentCount = 3;
        subpasses[1].pInputAttachments = inputRefs;

        std::array<VkSubpassDependency, 3> deps{};
        // 外部 -> 子通道0：开始写 GBuffer/深度
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = 0;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        // 子通道0 -> 子通道1：GBuffer 写 -> 延迟光照读（输入附件）
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = 1;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        // 子通道1 -> 外部：交换链颜色写完成，供 UI 覆盖层 LOAD
        deps[2].srcSubpass = 1;
        deps[2].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[2].dstAccessMask = 0;

        VkRenderPassCreateInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        passInfo.attachmentCount = static_cast<uint32_t>(atts.size());
        passInfo.pAttachments = atts.data();
        passInfo.subpassCount = 2;
        passInfo.pSubpasses = subpasses;
        passInfo.dependencyCount = static_cast<uint32_t>(deps.size());
        passInfo.pDependencies = deps.data();

        const VkResult res = vkCreateRenderPass(ctx_.Device(), &passInfo, nullptr, &deferredRenderPass_);
        if (res != VK_SUCCESS)
            throw std::runtime_error("Renderer: 创建延迟渲染通道失败");
    }

    void Renderer::destroyDeferredRenderPass()
    {
        if (deferredRenderPass_ != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(ctx_.Device(), deferredRenderPass_, nullptr);
            deferredRenderPass_ = VK_NULL_HANDLE;
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

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            gAlbedoImages_[i].Create(ctx_, extent.width, extent.height, fmt.albedo,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            gNormalImages_[i].Create(ctx_, extent.width, extent.height, fmt.normal,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            gPositionImages_[i].Create(ctx_, extent.width, extent.height, fmt.position,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
            gDepthImages_[i].Create(ctx_, extent.width, extent.height, depthFormat_,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

            VkImageView views[5] = {
                gAlbedoImages_[i].View(),
                gNormalImages_[i].View(),
                gPositionImages_[i].View(),
                gDepthImages_[i].View(),
                swapchain_.Views()[i]
            };
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = deferredRenderPass_;
            fbInfo.attachmentCount = 5;
            fbInfo.pAttachments = views;
            fbInfo.width = extent.width;
            fbInfo.height = extent.height;
            fbInfo.layers = 1;
            VK_CHECK(vkCreateFramebuffer(ctx_.Device(), &fbInfo, nullptr, &deferredFramebuffers_[i]),
                "创建延迟渲染帧缓冲");
        }
    }

    void Renderer::destroyDeferredFramebuffers()
    {
        for (VkFramebuffer fb : deferredFramebuffers_)
            if (fb != VK_NULL_HANDLE)
                vkDestroyFramebuffer(ctx_.Device(), fb, nullptr);
        deferredFramebuffers_.clear();
        for (Image& img : gAlbedoImages_) img.Destroy();
        for (Image& img : gNormalImages_) img.Destroy();
        for (Image& img : gPositionImages_) img.Destroy();
        for (Image& img : gDepthImages_) img.Destroy();
        gAlbedoImages_.clear();
        gNormalImages_.clear();
        gPositionImages_.clear();
        gDepthImages_.clear();
    }

    void Renderer::SetDeferred(bool enabled)
    {
        if (enabled == deferredEnabled_)
            return;
        deferredEnabled_ = enabled;
        if (enabled)
        {
            createDeferredResources(); // 创建渲染通道 + GBuffer 图像 + 帧缓冲
            LOG_INFO("延迟渲染已启用（GBuffer MRT + 输入附件延迟光照）");
        }
        else
        {
            destroyDeferredResources();
            LOG_INFO("延迟渲染已关闭，回退前向渲染");
        }
    }

    void Renderer::createDeferredResources()
    {
        createDeferredRenderPass();
        createDeferredFramebuffers();
    }

    void Renderer::destroyDeferredResources()
    {
        // 仅释放 GBuffer 帧缓冲与图像；延迟渲染通道本身始终保留（与交换链格式同步，
        // 供 GBuffer/光照管线持续引用），避免开关延迟模式后管线引用到已销毁的渲染通道。
        destroyDeferredFramebuffers();
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
        const std::function<void(VkCommandBuffer, uint32_t, uint32_t, VkExtent2D)>& recordLighting)
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
        const VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain_.Handle(), UINT64_MAX,
            imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            handleResize();
            return;
        }
        if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR)
            VK_CHECK(acquireResult, "获取交换链图像");

        VK_CHECK(vkResetFences(device, 1, &inFlightFence), "重置帧栅栏");

        // ---- 录制命令 ----
        VkCommandBuffer cmd = commandBuffers_[currentFrame_];
        VK_CHECK(vkResetCommandBuffer(cmd, 0), "重置命令缓冲");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "开始录制命令缓冲");

        // 重置本帧时间戳查询池，避免读到上一轮残留结果
        if (gpuProfiler_)
            gpuProfiler_->Reset(cmd, currentFrame_);

        // 深度预通道（阴影贴图等），在主渲染通道之前执行
        if (gpuProfiler_)
            gpuProfiler_->Write(cmd, currentFrame_, 0); // 阴影预通道开始
        if (prePass)
            prePass(cmd, currentFrame_, swapchain_.Extent());
        if (gpuProfiler_)
            gpuProfiler_->Write(cmd, currentFrame_, 1); // 场景通道开始（=阴影结束）

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color.float32[0] = 0.08f;
        clearValues[0].color.float32[1] = 0.09f;
        clearValues[0].color.float32[2] = 0.12f;
        clearValues[0].color.float32[3] = 1.0f;
        clearValues[1].depthStencil = { 1.0f, 0 };

        // 前向场景通道：仅非延迟模式下录制。延迟模式由双子通道延迟渲染通道完成，
        // 此处必须跳过，否则 recordScene 会把 GBuffer 管线误绑到前向渲染通道（附件数不匹配）。
        if (!deferredEnabled_)
        {
            VkRenderPassBeginInfo passInfo{};
            passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            passInfo.renderPass = renderPass_.renderPass;
            passInfo.framebuffer = framebuffers_[imageIndex];
            passInfo.renderArea.offset = { 0, 0 };
            passInfo.renderArea.extent = swapchain_.Extent();
            passInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            passInfo.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
            recordScene(cmd, currentFrame_, swapchain_.Extent());
            vkCmdEndRenderPass(cmd);
        }

        // 延迟渲染：GBuffer 几何子通道 -> 延迟光照子通道（输入附件采样 GBuffer）
        if (deferredEnabled_)
        {
            if (gpuProfiler_)
                gpuProfiler_->Write(cmd, currentFrame_, 1); // 几何子通道开始（=阴影结束）

            std::array<VkClearValue, 5> deferredClears{};
            deferredClears[0].color = { 0.0f, 0.0f, 0.0f, 0.0f }; // 反照率（背景 alpha=0）
            deferredClears[1].color = { 0.0f, 0.0f, 0.0f, 0.0f }; // 法线
            deferredClears[2].color = { 0.0f, 0.0f, 0.0f, 0.0f }; // 世界坐标（a=几何标记=0 => 背景）
            deferredClears[3].depthStencil = { 1.0f, 0 };
            deferredClears[4].color = { 0.08f, 0.09f, 0.12f, 1.0f };

            VkRenderPassBeginInfo dPassInfo{};
            dPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            dPassInfo.renderPass = deferredRenderPass_;
            dPassInfo.framebuffer = deferredFramebuffers_[imageIndex];
            dPassInfo.renderArea.offset = { 0, 0 };
            dPassInfo.renderArea.extent = swapchain_.Extent();
            dPassInfo.clearValueCount = static_cast<uint32_t>(deferredClears.size());
            dPassInfo.pClearValues = deferredClears.data();

            vkCmdBeginRenderPass(cmd, &dPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            recordScene(cmd, currentFrame_, swapchain_.Extent()); // 子通道0：几何写 GBuffer
            if (gpuProfiler_)
                gpuProfiler_->Write(cmd, currentFrame_, 2); // 延迟光照开始（=几何结束）
            vkCmdNextSubpass(cmd, VK_SUBPASS_CONTENTS_INLINE);
            if (recordLighting)
                recordLighting(cmd, currentFrame_, imageIndex, swapchain_.Extent()); // 子通道1：延迟光照
            vkCmdEndRenderPass(cmd);

            if (gpuProfiler_)
                gpuProfiler_->Write(cmd, currentFrame_, 3); // UI 通道开始（=延迟光照结束）
        }

        // UI覆盖层通道：在场景渲染之上绘制界面，并完成到PRESENT布局的转换
        if (gpuProfiler_)
            gpuProfiler_->Write(cmd, currentFrame_, 2); // UI 通道开始（=场景结束）
        if (recordUi)
            recordUi(cmd, imageIndex, swapchain_.Extent());
        if (gpuProfiler_)
            gpuProfiler_->Write(cmd, currentFrame_, 3); // 整帧结束（=UI 结束）

        VK_CHECK(vkEndCommandBuffer(cmd), "结束录制命令缓冲");

        // ---- 提交 ----
        const VkSemaphore waitSemaphore = imageAvailableSemaphores_[currentFrame_];
        const VkSemaphore signalSemaphore = renderFinishedSemaphores_[imageIndex];

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &waitSemaphore;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
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
            window_.ConsumeResizedFlag())
        {
            handleResize();
        }
        else if (presentResult != VK_SUCCESS)
        {
            VK_CHECK(presentResult, "呈现图像");
        }

        currentFrame_ = (currentFrame_ + 1) % kMaxFrames;
    }
}
