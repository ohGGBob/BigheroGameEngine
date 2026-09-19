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
        renderPass_.Create(ctx_.Device(), SceneColorFormat(), depthFormat_, sampleCount_);
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
            postProcessor_.Init(ctx_, swapchain_.Extent(), SceneColorFormat(), sampleCount_, swapchain_.Views());
        }
        else
        {
            postProcessor_.Recreate(ctx_, swapchain_.Extent(), swapchain_.Views());
        }
        createOffscreenFramebuffer();
    }

    if (resizeCallback_)
        resizeCallback_();

    // 兜底：对账所有依赖交换链图像数的资源，防未来新增 per-image 资源漏掉重建链
    validateSwapchainDependentResources();
}

void Renderer::validateSwapchainDependentResources()
{
    const size_t n = swapchain_.ImageCount();

    // 这两组无条件随交换链重建，任何路径下尺寸都必须等于新图像数
    if (framebuffers_.size() != n)
        LOG_WARN("[handleResize] framebuffers_ 尺寸 " << framebuffers_.size() << " != 交换链图像数 " << n);
    if (renderFinishedSemaphores_.size() != n)
        LOG_WARN("[handleResize] renderFinishedSemaphores_ 尺寸 " << renderFinishedSemaphores_.size()
                                                                  << " != 交换链图像数 " << n);

    const auto warnDrift = [&](const char* name, size_t size)
    {
        if (size != 0 && size != n)
            LOG_WARN("[handleResize] `" << name << "` 尺寸 " << size << " 与交换链图像数 " << n
                                        << " 不一致（应随交换链重建或保持为空）");
    };

    if (deferredEnabled_)
    {
        const bool drift = deferredFramebuffers_.size() != n || lightingFramebuffers_.size() != n ||
                           transparentFramebuffers_.size() != n || gAlbedoImages_.size() != n ||
                           gNormalImages_.size() != n || gPositionImages_.size() != n || gDepthImages_.size() != n ||
                           compositeFramebuffers_.size() != n;
        if (drift)
        {
            // 自愈：延迟模式 per-image 资源全部由 createDeferredFramebuffers 重建（含合成帧缓冲）。
            // 本函数在 handleResize 末尾执行：开头已 WaitIdle 且此后尚无新提交，重建安全。
            LOG_WARN("[handleResize] 延迟 per-image 资源与交换链图像数不一致，自愈重建");
            destroyDeferredFramebuffers();
            createDeferredFramebuffers();
        }
    }
    else
    {
        // 前向模式这些向量应保持为空（未启用延迟时由 SetDeferred(false) 清理）；
        // 若出现非空且尺寸不符的残留，仅告警（延迟 pass 不会在前向路径执行）。
        // compositeFramebuffers_ 允许为空或过期：合成 pass 位于 Renderer.cpp 的
        // if (deferredEnabled_) 块内，前向路径不消费该向量；此处仅告警观察异常状态，
        // 不重建（createCompositeResources 管线段会读 SPV 并建 VkPipeline，非必需路径不引入）。
        warnDrift("deferredFramebuffers_", deferredFramebuffers_.size());
        warnDrift("lightingFramebuffers_", lightingFramebuffers_.size());
        warnDrift("transparentFramebuffers_", transparentFramebuffers_.size());
        warnDrift("gAlbedoImages_", gAlbedoImages_.size());
        warnDrift("gNormalImages_", gNormalImages_.size());
        warnDrift("gPositionImages_", gPositionImages_.size());
        warnDrift("gDepthImages_", gDepthImages_.size());
        warnDrift("compositeFramebuffers_", compositeFramebuffers_.size());
    }
}
} // namespace BigHero
