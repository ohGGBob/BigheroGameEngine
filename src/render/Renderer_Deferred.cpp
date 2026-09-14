// 阶段 4：Renderer.cpp 物理拆分 TU 之二 —— 延迟渲染通道与 GBuffer 资源。
// 承接：几何/光照/透明三个渲染通道的创建销毁、GBuffer 图像与帧缓冲、
//       SetDeferred 开关（延迟↔前向切换）、GBuffer 视图访问器。
// 方法本体自 Renderer.cpp 原样迁出（纯重构，不改行为）。

#include "render/Renderer.h"

#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Context.h"

#include <array>
#include <memory>

namespace BigHero
{
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
    // 深度附件：STORE 供透明叠加通道只读深度测试（透明体不得遮挡不透明几何）
    atts[3].format = depthFormat_;
    atts[3].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    atts[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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

void Renderer::createTransparentRenderPass()
{
    if (transparentRenderPass_ != VK_NULL_HANDLE)
        return;

    // 透明叠加通道：把 BLEND/加性自发光物体混合到离屏 HDR 颜色上。
    //   颜色附件 loadOp=LOAD（保留光照 Pass 结果，不清除）+ 管线混合；
    //   深度附件只读（loadOp=LOAD，管线 depthWrite=false）做深度测试，
    //   使透明体被不透明几何正确遮挡（深度沿用 GBuffer 写入结果）。
    std::array<VkAttachmentDescription, 2> atts{};
    atts[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // 渲染图已把光照输出转换回颜色附件布局；结束后转 SHADER_READ_ONLY 供 SSR/合成采样
    atts[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[1].format = depthFormat_;
    atts[1].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    atts[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    const VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    // 外部依赖：颜色 LOAD 读取光照 Pass 写入 + 深度测试读取 GBuffer 深度写入
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = static_cast<uint32_t>(atts.size());
    info.pAttachments = atts.data();
    info.subpassCount = 1;
    info.pSubpasses = &sub;
    info.dependencyCount = 1;
    info.pDependencies = &dep;

    VK_CHECK(vkCreateRenderPass(ctx_.Device(), &info, nullptr, &transparentRenderPass_), "创建透明叠加渲染通道");
}

void Renderer::destroyTransparentRenderPass()
{
    if (transparentRenderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(ctx_.Device(), transparentRenderPass_, nullptr);
        transparentRenderPass_ = VK_NULL_HANDLE;
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
    transparentFramebuffers_.resize(imageCount);

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

        // 透明叠加通道帧缓冲：离屏 HDR 颜色（LOAD+混合）+ GBuffer 深度（只读测试）
        VkImageView transparentViews[2] = {offscreenColorImage_->View(), gDepthImages_[i].View()};
        VkFramebufferCreateInfo transFb{};
        transFb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        transFb.renderPass = transparentRenderPass_;
        transFb.attachmentCount = 2;
        transFb.pAttachments = transparentViews;
        transFb.width = extent.width;
        transFb.height = extent.height;
        transFb.layers = 1;
        VK_CHECK(vkCreateFramebuffer(ctx_.Device(), &transFb, nullptr, &transparentFramebuffers_[i]),
                 "创建透明叠加帧缓冲");
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
    for (VkFramebuffer fb : transparentFramebuffers_)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(ctx_.Device(), fb, nullptr);
    transparentFramebuffers_.clear();
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
    createTransparentRenderPass();
    createDeferredFramebuffers();
}

void Renderer::destroyDeferredResources()
{
    // 仅释放 GBuffer 帧缓冲与图像；渲染通道本身始终保留（与交换链格式同步，
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
} // namespace BigHero
