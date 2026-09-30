// 反射探针彩色立方图捕获目标实现（U2-L2 GPU 捕获 v1，单探针）。
// 结构对照 CubeShadowMap（立方图 + 6 面帧缓冲 + 独立渲染通道），差异：
// 颜色附件为 RGBA16F（HDR 线性）、带 mip 链（FinishFrame 内 vkCmdBlitImage 逐级降采样，
// 供 roughness→LOD 的预过滤近似），布局状态帧间显式管理。
#include "render/ReflectionCapture.h"

#include "core/VkCheck.h"
#include "render/Context.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace BigHero
{
void ReflectionCapture::Create(const Context& ctx, uint32_t size, uint32_t mipLevels)
{
    ctx_ = &ctx;
    size_ = std::max(4u, size);
    mipLevels_ = std::clamp(mipLevels, 1u, static_cast<uint32_t>(std::log2(static_cast<double>(size_))) + 1u);

    // 颜色立方图：HDR 线性（捕获输出不做色调映射），可渲染/可采样/可 blit
    colorImage_.Destroy();
    colorImage_.Create(ctx, size_, size_, VK_FORMAT_R16G16B16A16_SFLOAT,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels_,
                       VK_SAMPLE_COUNT_1_BIT, kFaceCount, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);
    // 深度立方图：仅附件用途，逐面 loadOp=CLEAR、finalLayout=UNDEFINED（无需管理）
    depthImage_.Destroy();
    depthImage_.Create(ctx, size_, size_, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_SAMPLE_COUNT_1_BIT,
                       kFaceCount, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);

    VkSamplerCreateInfo samp{};
    samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp.magFilter = VK_FILTER_LINEAR;
    samp.minFilter = VK_FILTER_LINEAR;
    samp.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.maxLod = static_cast<float>(mipLevels_ - 1); // 单图上限：levelCount-1（VUID-VkSamplerCreateInfo-maxLod-01973）
    VK_CHECK(vkCreateSampler(ctx.Device(), &samp, nullptr, &sampler_), "创建反射探针捕获采样器");

    // 渲染通道：color（COLOR_ATTACHMENT 进出，HDR 线性）+ depth（UNDEFINED 进出，逐面清屏）
    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // loadOp=CLEAR 丢弃旧内容，首帧免屏障
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // don't-care（loadOp=CLEAR 逐面清屏）
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // 不得为 UNDEFINED（VUID-00843）

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    // 合并深度模板布局（separateDepthStencilLayouts 特性未启用，VUID-03313）
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 2;
    passInfo.pAttachments = attachments.data();
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    VK_CHECK(vkCreateRenderPass(ctx.Device(), &passInfo, nullptr, &renderPass_), "创建反射探针捕获渲染通道");

    // 每面 2D 视图（mip0）+ 帧缓冲
    for (int f = 0; f < kFaceCount; ++f)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = colorImage_.Get();
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, static_cast<uint32_t>(f), 1};
        VK_CHECK(vkCreateImageView(ctx.Device(), &viewInfo, nullptr, &colorFaceViews_[f]), "创建捕获颜色面视图");

        VkImageViewCreateInfo depthViewInfo{};
        depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image = depthImage_.Get();
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = VK_FORMAT_D32_SFLOAT;
        depthViewInfo.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, static_cast<uint32_t>(f), 1};
        VK_CHECK(vkCreateImageView(ctx.Device(), &depthViewInfo, nullptr, &depthFaceViews_[f]), "创建捕获深度面视图");

        std::array<VkImageView, 2> fbAttachments = {colorFaceViews_[f], depthFaceViews_[f]};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass_;
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = fbAttachments.data();
        fbInfo.width = size_;
        fbInfo.height = size_;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(ctx.Device(), &fbInfo, nullptr, &framebuffers_[f]), "创建捕获帧缓冲");
    }
    colorInShaderRead_ = false; // 新建图像布局为 UNDEFINED，首帧 PrepareFrame 正向转换
}

void ReflectionCapture::Destroy()
{
    if (ctx_ == nullptr)
        return;
    const VkDevice device = ctx_->Device();
    for (VkFramebuffer fb : framebuffers_)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, fb, nullptr);
    framebuffers_.fill(VK_NULL_HANDLE);
    for (VkImageView v : colorFaceViews_)
        if (v != VK_NULL_HANDLE)
            vkDestroyImageView(device, v, nullptr);
    colorFaceViews_.fill(VK_NULL_HANDLE);
    for (VkImageView v : depthFaceViews_)
        if (v != VK_NULL_HANDLE)
            vkDestroyImageView(device, v, nullptr);
    depthFaceViews_.fill(VK_NULL_HANDLE);
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
    colorImage_.Destroy();
    depthImage_.Destroy();
    ctx_ = nullptr;
}

void ReflectionCapture::RecordFace(VkCommandBuffer cmd, int face,
                                   const std::function<void(VkCommandBuffer, int face)>& drawScene)
{
    VkClearValue clears[2]{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = renderPass_;
    passInfo.framebuffer = framebuffers_[static_cast<size_t>(face)];
    passInfo.renderArea = {{0, 0}, {size_, size_}};
    passInfo.clearValueCount = 2;
    passInfo.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(size_);
    viewport.height = static_cast<float>(size_);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, {size_, size_}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    drawScene(cmd, face);
    vkCmdEndRenderPass(cmd);
}

void ReflectionCapture::PrepareFrame(VkCommandBuffer cmd)
{
    // 布局状态： FinishFrame 后全 mip 处于 SHADER_READ_ONLY（colorInShaderRead_=true），
    // 需转回 COLOR_ATTACHMENT 供本帧捕获通道使用；首帧图像为 UNDEFINED 且颜色附件
    // initialLayout=UNDEFINED（loadOp=CLEAR），无需屏障。
    if (!colorInShaderRead_)
        return;
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = colorImage_.Get();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels_, 0, kFaceCount};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    colorInShaderRead_ = false;
}

void ReflectionCapture::FinishFrame(VkCommandBuffer cmd)
{
    if (colorInShaderRead_)
        return; // 本帧未录制（旁路/异常），无需 mip 与布局转换
    const uint32_t layers = static_cast<uint32_t>(kFaceCount);
    auto fullRange = [&](uint32_t baseMip, uint32_t levelCount)
    {
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = baseMip;
        range.levelCount = levelCount;
        range.baseArrayLayer = 0;
        range.layerCount = layers;
        return range;
    };
    auto colorBarrier = [&](uint32_t baseMip, uint32_t levelCount, VkImageLayout oldL, VkImageLayout newL,
                            VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                            VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = colorImage_.Get();
        b.subresourceRange = fullRange(baseMip, levelCount);
        b.srcAccessMask = srcAccess;
        b.dstAccessMask = dstAccess;
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    };

    // mip0：COLOR_ATTACHMENT → TRANSFER_SRC
    colorBarrier(0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // 逐级 blit 降采样：level m ← level m-1（6 面各自独立）。统一布局路径：
    // 每个 mip 先 COLOR_ATTACHMENT→TRANSFER_DST（PrepareFrame 已把全图置于 COLOR_ATTACHMENT），
    // blit 后转 TRANSFER_SRC 供下一级读取。
    for (uint32_t mip = 1; mip < mipLevels_; ++mip)
    {
        colorBarrier(mip, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        for (uint32_t layer = 0; layer < layers; ++layer)
        {
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, layer, 1};
            blit.srcOffsets[1] = {static_cast<int32_t>(size_ >> (mip - 1)), static_cast<int32_t>(size_ >> (mip - 1)),
                                  1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1};
            blit.dstOffsets[1] = {static_cast<int32_t>(size_ >> mip), static_cast<int32_t>(size_ >> mip), 1};
            vkCmdBlitImage(cmd, colorImage_.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, colorImage_.Get(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        }
        colorBarrier(mip, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
    }

    // 全 mip：TRANSFER_SRC → SHADER_READ_ONLY（主场景同提交内采样）
    colorBarrier(0, mipLevels_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    colorInShaderRead_ = true;
}
} // namespace BigHero