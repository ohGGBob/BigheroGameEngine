// 阶段 4：Renderer.cpp 物理拆分 TU 之一 —— 帧资源生命周期。
// 承接：帧缓冲/MSAA 中间图像的创建销毁、信号量/栅栏同步对象、
//       以及窗口尺寸变化时的交换链重建编排（handleResize）。
// 方法本体自 Renderer.cpp 原样迁出（纯重构，不改行为）。

#include "render/Renderer.h"

#include "core/Log.h"
#include "core/VkCheck.h"
#include "platform/Window.h"
#include "render/Context.h"

#include <utility>

namespace BigHero
{
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
    if (ctx_.IsHeadless())
        return;

    ctx_.WaitIdle();
    destroyFrameResources();
    destroySyncObjects();

    // 借助oldSwapchain创建新交换链，随后释放旧资源
    Swapchain fresh;
    fresh.Create(ctx_, *window_, swapchain_.Handle());
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

    // 延迟渲染：格式变化时重建几何/光照/透明通道（否则管线引用过期通道）
    if (formatChanged)
    {
        destroyDeferredRenderPass();
        destroyLightingRenderPass();
        destroyTransparentRenderPass();
        createDeferredRenderPass();
        createLightingRenderPass();
        createTransparentRenderPass();
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
} // namespace BigHero
