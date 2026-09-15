// 阶段 4：Renderer.cpp 物理拆分 TU 之三 —— 后处理 / SSAO / SSR / 合成资源。
// 承接：SetSSAO/SetSSR/SetPostProcessing 功能开关、离屏帧缓冲（场景渲染色目标）、
//       合成通道（渲染通道/帧缓冲/采样器/描述符/管线）的创建销毁。
// 方法本体自 Renderer.cpp 原样迁出（纯重构，不改行为）。

#include "render/Renderer.h"

#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Context.h"
#include "render/pipeline.h"
#include "render/shader_loader.h"

#include <array>
#include <memory>

namespace BigHero
{
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
    // SSR 图像进出 transient 池（gDepth↔SSR 反射图别名共享显存）：池显存释放要求全部
    // 绑定图像已销毁，故重建 GBuffer（未绑定态）并释放池；新池槽位由下一帧 DrawFrame
    // 开头的 transientBindDirty_ 统一分配绑定
    ctx_.WaitIdle();
    ssrEnabled_ = enabled;
    ssr_.Destroy();
    destroyDeferredFramebuffers();
    transientAlloc_.Destroy();
    transientBound_ = false;
    createDeferredFramebuffers();
    if (enabled)
    {
        ssr_.Init(ctx_, swapchain_.Extent());
        LOG_INFO("SSR 已启用（最大距离 " << ssr_.maxDistance << "，步数 " << ssr_.stepCount << "）");
    }
    else
    {
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

    // 合成渲染通道：输出到交换链。finalLayout 保持 COLOR_ATTACHMENT_OPTIMAL，
    // 与渲染图 composite pass 声明的 endLayout 一致；由后续 UI 通道转换到 PRESENT_SRC
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
        att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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
} // namespace BigHero
