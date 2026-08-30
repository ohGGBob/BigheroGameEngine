#include "render/Renderer.h"
#include "render/Context.h"
#include "platform/Window.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "core/VkUtils.h"

#include <array>
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
        createFrameResources();
        createCommandResources();
        createSyncObjects();

        LOG_INFO("渲染器初始化完成，帧并行数: " << kMaxFrames
            << "，MSAA采样数: " << static_cast<uint32_t>(sampleCount_) << "x");
    }

    Renderer::~Renderer()
    {
        ctx_.WaitIdle();
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
        }

        createFrameResources();
        createSyncObjects();
        LOG_INFO("交换链已重建: " << swapchain_.Extent().width << "x" << swapchain_.Extent().height);

        if (resizeCallback_)
            resizeCallback_();
    }

    void Renderer::DrawFrame(const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordScene,
        const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& recordUi,
        const std::function<void(VkCommandBuffer, uint32_t, VkExtent2D)>& prePass)
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

        // 深度预通道（阴影贴图等），在主渲染通道之前执行
        if (prePass)
            prePass(cmd, currentFrame_, swapchain_.Extent());

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color.float32[0] = 0.08f;
        clearValues[0].color.float32[1] = 0.09f;
        clearValues[0].color.float32[2] = 0.12f;
        clearValues[0].color.float32[3] = 1.0f;
        clearValues[1].depthStencil = { 1.0f, 0 };

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

        // UI覆盖层通道：在场景渲染之上绘制界面，并完成到PRESENT布局的转换
        if (recordUi)
            recordUi(cmd, imageIndex, swapchain_.Extent());

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
