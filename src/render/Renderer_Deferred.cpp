// 阶段 4：Renderer.cpp 物理拆分 TU 之二 —— 延迟渲染通道与 GBuffer 资源。
// 承接：几何/光照/透明三个渲染通道的创建销毁、GBuffer 图像与帧缓冲、
//       SetDeferred 开关（延迟↔前向切换）、GBuffer 视图访问器。
// 方法本体自 Renderer.cpp 原样迁出（纯重构，不改行为）。

#include "render/Renderer.h"

#include "core/Log.h"
#include "core/VkCheck.h"
#include "core/VkUtils.h"
#include "render/Context.h"

#include <array>
#include <memory>
#include <stdexcept>

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
        // GBuffer 图像：颜色附件 + 可采样（SSAO/光照 Pass 纹理采样）。
        // 以未绑定态创建：显存由 bindTransientImages 分配 transient 池共享槽位
        // （各交换链槽位实例共享同一偏移 + SSR 反射图别名，降低显存峰值）。
        // 视图/帧缓冲不在此处创建（图像尚未绑显存），统一在 bindTransientImages 绑定后
        // 由 createDeferredFramebufferObjects 创建，严格满足 VUID-01020。
        gAlbedoImages_[i].CreateUnbound(ctx_, extent.width, extent.height, fmt.albedo,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                        VK_IMAGE_ASPECT_COLOR_BIT);
        gNormalImages_[i].CreateUnbound(ctx_, extent.width, extent.height, fmt.normal,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                        VK_IMAGE_ASPECT_COLOR_BIT);
        gPositionImages_[i].CreateUnbound(ctx_, extent.width, extent.height, fmt.position,
                                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                          VK_IMAGE_ASPECT_COLOR_BIT);
        gDepthImages_[i].CreateUnbound(ctx_, extent.width, extent.height, depthFormat_,
                                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

        // 离屏 HDR 颜色缓冲（非交换链；自管显存，在 Create 内绑定后即建视图，无 01020 问题）
        if (!offscreenColorImage_)
        {
            offscreenColorImage_ = std::make_unique<Image>();
            offscreenColorImage_->Create(ctx_, extent.width, extent.height, VK_FORMAT_R16G16B16A16_SFLOAT,
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        }
    }

    // 合成通道帧缓冲：绑定到交换链图像
    createCompositeResources();

    // GBuffer 图像集已变化（未绑定态）：待下一帧 DrawFrame 开头统一分配 transient 池共享槽位，
    // 随后创建帧缓冲（视图须在绑内存后创建）
    transientBindDirty_ = true;
}

// 取 GBuffer/离屏图像视图创建几何/光照/透明帧缓冲。调用时机：transient 池绑定显存之后
// （bindTransientImages 末尾）——此时 CreateUnbound 暂存的视图已创建，.View() 有效，
// 且严格满足 VUID-01020。
void Renderer::createDeferredFramebufferObjects()
{
    const VkExtent2D extent = swapchain_.Extent();
    // 以帧缓冲向量的实际大小为循环上界（而非 swapchain_.ImageCount()）：二者在
    // createDeferredFramebuffers 中同步 resize，若交换链重建后计数短暂不一致，
    // 以向量为准可避免越界；任一向量为空（延迟未启用/尚未创建）则直接返回。
    const size_t imageCount = deferredFramebuffers_.size();
    if (imageCount == 0 || gAlbedoImages_.size() != imageCount || gNormalImages_.size() != imageCount ||
        gPositionImages_.size() != imageCount || gDepthImages_.size() != imageCount ||
        lightingFramebuffers_.size() != imageCount || transparentFramebuffers_.size() != imageCount)
        return;
    if (offscreenColorImage_ == nullptr)
        return;

    for (size_t i = 0; i < imageCount; ++i)
    {
        if (deferredFramebuffers_[i] == VK_NULL_HANDLE)
        {
            // 几何通道帧缓冲：3 GBuffer + 深度
            VkImageView views[4] = {gAlbedoImages_[i].View(), gNormalImages_[i].View(), gPositionImages_[i].View(),
                                    gDepthImages_[i].View()};
            // 防御：视图应已在 bindTransientImages 中随内存绑定创建。若仍为空，说明时序有误，
            // 直接抛错而非把空视图传给 vkCreateFramebuffer（后者触发 VUID 违规/驱动崩溃）。
            for (VkImageView v : views)
                if (v == VK_NULL_HANDLE)
                    throw std::runtime_error("createDeferredFramebufferObjects: GBuffer 视图为空（时序错误）");
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = deferredRenderPass_;
            fbInfo.attachmentCount = 4;
            fbInfo.pAttachments = views;
            fbInfo.width = extent.width;
            fbInfo.height = extent.height;
            fbInfo.layers = 1;
            VK_CHECK(vkCreateFramebuffer(ctx_.Device(), &fbInfo, nullptr, &deferredFramebuffers_[i]),
                     "创建延迟几何帧缓冲");
        }

        if (lightingFramebuffers_[i] == VK_NULL_HANDLE)
        {
            // 光照通道帧缓冲：离屏 HDR 颜色缓冲（非交换链）
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

        if (transparentFramebuffers_[i] == VK_NULL_HANDLE)
        {
            // 透明叠加通道帧缓冲：离屏 HDR 颜色（LOAD+混合）+ GBuffer 深度（只读测试）
            VkImageView transparentViews[2] = {offscreenColorImage_->View(), gDepthImages_[i].View()};
            for (VkImageView v : transparentViews)
                if (v == VK_NULL_HANDLE)
                    throw std::runtime_error("createDeferredFramebufferObjects: 透明通道视图为空（时序错误）");
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
    }
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
    // 此时全部池绑定图像（GBuffer + SSR）均已销毁，可安全释放 transient 池显存
    transientAlloc_.Destroy();
    transientBound_ = false;
    transientBindDirty_ = false;
}

// TransientAllocator 池化绑定：把离屏图像按生命周期别名共享 device-local 显存。
// 槽位规划（槽位内图像绑定到同一偏移，互为别名）：
//   0: gAlbedo 各交换链槽位实例   1: gNormal   2: gPosition
//   3: gDepth 各槽位实例 + SSR 反射图（生命周期不重叠：gDepth [gbuffer,transparent] vs
//      ssrReflection [ssr,composite]，渲染图 DeclareAlias 生成覆写屏障）
//   4: SSR 模糊图（仅 SSR 激活时）
// 各槽位实例跨帧由同队列提交串行 + 渲染图首用屏障源并集（含上帧残留访问）保证覆写安全。
// 由 DrawFrame 开头的 transientBindDirty_ 触发；调用时全部槽位图像均已（重）建且未绑定。
void Renderer::bindTransientImages()
{
    transientBound_ = false;
    if (gDepthImages_.empty())
        return;
    const auto ready = [this](const std::vector<Image>& imgs)
    {
        for (const Image& img : imgs)
            if (img.Get() == VK_NULL_HANDLE)
                return false;
        return !imgs.empty();
    };
    if (!ready(gAlbedoImages_) || !ready(gNormalImages_) || !ready(gPositionImages_) || !ready(gDepthImages_))
        return;

    const bool ssrActive =
        ssrEnabled_ && ssr_.IsValid() && ssr_.ReflectionImage() != nullptr && ssr_.BlurImage() != nullptr;

    std::vector<std::vector<VkImage>> slotImages(5);
    std::vector<std::vector<VkMemoryRequirements>> slotReqs(5);
    std::vector<std::vector<Image*>> slotImageObjs(5); // 与 slotImages 平行：绑定后据其创建待建视图
    const auto addSlot = [this, &slotImages, &slotReqs, &slotImageObjs](size_t slot, Image& img)
    {
        slotImages[slot].push_back(img.Get());
        slotReqs[slot].push_back(img.MemoryRequirements(ctx_));
        slotImageObjs[slot].push_back(&img);
    };
    for (Image& img : gAlbedoImages_)
        addSlot(0, img);
    for (Image& img : gNormalImages_)
        addSlot(1, img);
    for (Image& img : gPositionImages_)
        addSlot(2, img);
    for (Image& img : gDepthImages_)
        addSlot(3, img);
    if (ssrActive)
    {
        addSlot(3, *ssr_.ReflectionImage());
        addSlot(4, *ssr_.BlurImage());
    }

    // 池大小 = Σ槽位(最大需求向上取整到最大对齐) + 每槽位一个对齐间隙
    VkDeviceSize poolSize = 0;
    uint32_t memTypeBits = ~0u;
    for (size_t s = 0; s < slotImages.size(); ++s)
    {
        if (slotImages[s].empty())
            continue;
        VkDeviceSize maxSize = 0;
        VkDeviceSize maxAlign = 1;
        for (const VkMemoryRequirements& req : slotReqs[s])
        {
            maxSize = std::max(maxSize, req.size);
            maxAlign = std::max(maxAlign, req.alignment);
            memTypeBits &= req.memoryTypeBits;
        }
        poolSize += ((maxSize + maxAlign - 1) / maxAlign) * maxAlign + maxAlign;
    }
    if (memTypeBits == 0)
        memTypeBits = slotReqs[0][0].memoryTypeBits;
    const uint32_t typeIdx = FindMemoryType(ctx_.PhysicalDevice(), memTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (typeIdx == UINT32_MAX)
        throw std::runtime_error("transient池: GBuffer/SSR 图像无公共 DEVICE_LOCAL 内存类型");

    // 重建池（旧池绑定图像均已销毁：重建路径先行 WaitIdle + 图像销毁）
    transientAlloc_.Destroy();
    transientAlloc_.Create(ctx_, poolSize, typeIdx);
    if (!transientAlloc_.IsValid())
        throw std::runtime_error("transient池: 创建失败");

    for (size_t s = 0; s < slotImages.size(); ++s)
    {
        if (slotImages[s].empty())
            continue;
        const VkDeviceSize off = transientAlloc_.AllocateAndBindShared(slotImages[s].data(), slotReqs[s].data(),
                                                                       static_cast<uint32_t>(slotImages[s].size()));
        if (off == Render::TransientMemoryPool::kInvalidOffset)
            throw std::runtime_error("transient池: 共享槽位分配失败（池容量不足）");

        // 显存已绑定 → 此刻创建 CreateUnbound 暂存的视图（视图严格晚于内存绑定，满足 01020）
        for (Image* img : slotImageObjs[s])
            img->FinalizePendingView();
    }
    transientBound_ = true;
    LOG_INFO("[Transient] 池化绑定: GBuffer 各槽位实例共享显存" << (ssrActive ? "，SSR 反射别名至 GBuffer 深度槽" : "")
                                                                << "，池 " << poolSize << "B");

    // 视图已就绪：创建几何/光照/透明帧缓冲（须在 bind 之后，满足 VUID-01020）
    createDeferredFramebufferObjects();

    // SSR 反射/模糊图像同样在本轮绑定显存：其帧缓冲亦须此刻（bind 后）创建
    if (ssrActive)
        ssr_.CreateFramebuffers();

    // 通知外部（Application）：GBuffer 视图已就绪，可安全把它们写入描述符集
    if (transientBoundCallback_)
        transientBoundCallback_();
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
