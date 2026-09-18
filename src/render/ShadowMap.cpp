#include "render/ShadowMap.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "core/VkUtils.h"
#include "render/Context.h"

#include <stdexcept>

namespace BigHero
{
void ShadowMap::Create(const Context& ctx, uint32_t size)
{
    Destroy();
    ctx_ = &ctx;
    size_ = size;

    // 选用支持深度附件+线性过滤（PCF软采样）的深度格式
    const VkFormat depthFormat = FindSupportedFormat(
        ctx.PhysicalDevice(), {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
    if (depthFormat == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("ShadowMap: 未找到支持线性过滤的深度格式");

    // 深度图像：渲染通道写入 + 主通道采样
    depthImage_.Create(ctx, size_, size_, depthFormat,
                       VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

    // 采样器：边界外返回白色（视为无遮挡）
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    VK_CHECK(vkCreateSampler(ctx.Device(), &samplerInfo, nullptr, &sampler_), "创建阴影采样器");

    // 仅深度渲染通道：UNDEFINED载入（深度每帧清空重写）-> DEPTH_STENCIL_READ_ONLY（供主通道采样）。
    // 注意：separateDepthStencilLayouts 特性未启用，finalLayout 禁用单面布局
    // DEPTH_READ_ONLY_OPTIMAL（VUID-03285），须用双面布局 DEPTH_STENCIL_READ_ONLY_OPTIMAL
    VkAttachmentDescription depth{};
    depth.format = depthFormat;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // 0->EXTERNAL：子通道深度写入与 finalLayout 转换对后续采样方（主通道 Light 集/雾 shadow）
    // 建立内存依赖；缺失时阴影图可见性无任何保证（规范要求 finalLayout 转换由配对依赖同步）
    VkSubpassDependency lateDependency{};
    lateDependency.srcSubpass = 0;
    lateDependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    lateDependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    lateDependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    lateDependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    lateDependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    const VkSubpassDependency dependencies[2] = {dependency, lateDependency};

    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &depth;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 2;
    passInfo.pDependencies = dependencies;
    VK_CHECK(vkCreateRenderPass(ctx.Device(), &passInfo, nullptr, &renderPass_), "创建阴影渲染通道");

    const VkImageView depthView = depthImage_.View();
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &depthView;
    fbInfo.width = size_;
    fbInfo.height = size_;
    fbInfo.layers = 1;
    VK_CHECK(vkCreateFramebuffer(ctx.Device(), &fbInfo, nullptr, &framebuffer_), "创建阴影帧缓冲");

    LOG_INFO("阴影贴图初始化完成: " << size_ << "x" << size_);
}

void ShadowMap::Destroy()
{
    if (ctx_ == nullptr)
        return;

    const VkDevice device = ctx_->Device();
    if (framebuffer_ != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(device, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    depthImage_.Destroy();
    ctx_ = nullptr;
    size_ = 0;
}

void ShadowMap::RecordPass(VkCommandBuffer cmd,
                           const std::function<void(VkCommandBuffer, uint32_t cascade)>& drawScene) const
{
    VkClearValue clearDepth{};
    clearDepth.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = renderPass_;
    passInfo.framebuffer = framebuffer_;
    passInfo.renderArea.offset = {0, 0};
    passInfo.renderArea.extent = {size_, size_};
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clearDepth;

    vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

    // 逐级联在 2x2 图集子块内绘制：级联 c 占据列 (c&1)、行 (c/2) 的 tile
    const uint32_t tile = size_ / kCascadeCount;
    const float tileF = static_cast<float>(tile);
    for (uint32_t cascade = 0; cascade < kCascadeCount; ++cascade)
    {
        VkViewport viewport{};
        viewport.x = static_cast<float>(cascade & 1u) * tileF;
        viewport.y = static_cast<float>(cascade >> 1) * tileF;
        viewport.width = tileF;
        viewport.height = tileF;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{{static_cast<int32_t>(cascade & 1u) * static_cast<int32_t>(tile),
                          static_cast<int32_t>(cascade >> 1) * static_cast<int32_t>(tile)},
                         {tile, tile}};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        drawScene(cmd, cascade);
    }

    vkCmdEndRenderPass(cmd);
}
} // namespace BigHero
