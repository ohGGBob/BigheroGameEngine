#include "render/SSAO.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Context.h"
#include "render/image.h"
#include "render/pipeline.h"
#include "render/shader_loader.h"
#include <algorithm>
#include <array>

#define SSAO_VKCHECK(result) VK_CHECK(result, "SSAO")

namespace BigHero::Render
{
SSAO::~SSAO()
{
    Destroy();
}

SSAO::SSAO(SSAO&& other) noexcept
{
    device_ = other.device_;
    extent_ = other.extent_;
    halfExtent_ = other.halfExtent_;
    aoImage_ = std::move(other.aoImage_);
    aoBlurImage_ = std::move(other.aoBlurImage_);
    ssaoRenderPass_ = other.ssaoRenderPass_;
    blurRenderPass_ = other.blurRenderPass_;
    aoFramebuffer_ = other.aoFramebuffer_;
    aoBlurFramebuffer_ = other.aoBlurFramebuffer_;
    ssaoPipeline_ = std::move(other.ssaoPipeline_);
    blurPipeline_ = std::move(other.blurPipeline_);
    ssaoLayout_ = other.ssaoLayout_;
    blurLayout_ = other.blurLayout_;
    descPool_ = other.descPool_;
    ssaoSet_ = other.ssaoSet_;
    blurSet_ = other.blurSet_;
    sampler_ = other.sampler_;
    radius = other.radius;
    bias = other.bias;
    strength = other.strength;
    other.device_ = VK_NULL_HANDLE;
    other.ssaoRenderPass_ = VK_NULL_HANDLE;
    other.blurRenderPass_ = VK_NULL_HANDLE;
    other.aoFramebuffer_ = VK_NULL_HANDLE;
    other.aoBlurFramebuffer_ = VK_NULL_HANDLE;
    other.ssaoLayout_ = VK_NULL_HANDLE;
    other.blurLayout_ = VK_NULL_HANDLE;
    other.descPool_ = VK_NULL_HANDLE;
    other.ssaoSet_ = VK_NULL_HANDLE;
    other.blurSet_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
}

SSAO& SSAO::operator=(SSAO&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        device_ = other.device_;
        extent_ = other.extent_;
        halfExtent_ = other.halfExtent_;
        aoImage_ = std::move(other.aoImage_);
        aoBlurImage_ = std::move(other.aoBlurImage_);
        ssaoRenderPass_ = other.ssaoRenderPass_;
        blurRenderPass_ = other.blurRenderPass_;
        aoFramebuffer_ = other.aoFramebuffer_;
        aoBlurFramebuffer_ = other.aoBlurFramebuffer_;
        ssaoPipeline_ = std::move(other.ssaoPipeline_);
        blurPipeline_ = std::move(other.blurPipeline_);
        ssaoLayout_ = other.ssaoLayout_;
        blurLayout_ = other.blurLayout_;
        descPool_ = other.descPool_;
        ssaoSet_ = other.ssaoSet_;
        blurSet_ = other.blurSet_;
        sampler_ = other.sampler_;
        radius = other.radius;
        bias = other.bias;
        strength = other.strength;
        other.device_ = VK_NULL_HANDLE;
        other.ssaoRenderPass_ = VK_NULL_HANDLE;
        other.blurRenderPass_ = VK_NULL_HANDLE;
        other.aoFramebuffer_ = VK_NULL_HANDLE;
        other.aoBlurFramebuffer_ = VK_NULL_HANDLE;
        other.ssaoLayout_ = VK_NULL_HANDLE;
        other.blurLayout_ = VK_NULL_HANDLE;
        other.descPool_ = VK_NULL_HANDLE;
        other.ssaoSet_ = VK_NULL_HANDLE;
        other.blurSet_ = VK_NULL_HANDLE;
        other.sampler_ = VK_NULL_HANDLE;
    }
    return *this;
}

void SSAO::Init(const Context& ctx, VkExtent2D extent)
{
    device_ = ctx.Device();
    extent_ = extent;
    halfExtent_ = {std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)};

    CreateImages(ctx);
    CreateRenderPasses(ctx);
    CreateFramebuffers();
    CreateDescriptorResources(ctx);
    CreatePipelines(ctx);
}

void SSAO::Destroy() noexcept
{
    if (device_ == VK_NULL_HANDLE)
        return;
    DestroyPipelines();
    DestroyFramebuffers();
    if (ssaoRenderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, ssaoRenderPass_, nullptr);
        ssaoRenderPass_ = VK_NULL_HANDLE;
    }
    if (blurRenderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, blurRenderPass_, nullptr);
        blurRenderPass_ = VK_NULL_HANDLE;
    }
    if (descPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device_, descPool_, nullptr);
        descPool_ = VK_NULL_HANDLE;
    }
    if (ssaoLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_, ssaoLayout_, nullptr);
        ssaoLayout_ = VK_NULL_HANDLE;
    }
    if (blurLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_, blurLayout_, nullptr);
        blurLayout_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    aoImage_.reset();
    aoBlurImage_.reset();
    device_ = VK_NULL_HANDLE;
}

void SSAO::Recreate(const Context& ctx, VkExtent2D extent)
{
    DestroyFramebuffers();
    DestroyPipelines();
    aoImage_.reset();
    aoBlurImage_.reset();
    extent_ = extent;
    halfExtent_ = {std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)};
    CreateImages(ctx);
    CreateFramebuffers();
    CreatePipelines(ctx);
}

void SSAO::CreateImages(const Context& ctx)
{
    aoImage_ = std::make_unique<BigHero::Image>();
    aoImage_->Create(ctx, halfExtent_.width, halfExtent_.height, VK_FORMAT_R8_UNORM,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    aoBlurImage_ = std::make_unique<BigHero::Image>();
    aoBlurImage_->Create(ctx, halfExtent_.width, halfExtent_.height, VK_FORMAT_R8_UNORM,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
}

void SSAO::CreateRenderPasses(const Context& ctx)
{
    (void)ctx;
    // SSAO 渲染通道：单颜色附件，最终 SHADER_READ_ONLY
    VkAttachmentDescription att{};
    att.format = VK_FORMAT_R8_UNORM;
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
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
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
    SSAO_VKCHECK(vkCreateRenderPass(device_, &info, nullptr, &ssaoRenderPass_));

    // 模糊渲染通道：同上
    SSAO_VKCHECK(vkCreateRenderPass(device_, &info, nullptr, &blurRenderPass_));
}

void SSAO::CreateFramebuffers()
{
    VkImageView aoView = aoImage_->View();
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = ssaoRenderPass_;
    info.attachmentCount = 1;
    info.pAttachments = &aoView;
    info.width = halfExtent_.width;
    info.height = halfExtent_.height;
    info.layers = 1;
    SSAO_VKCHECK(vkCreateFramebuffer(device_, &info, nullptr, &aoFramebuffer_));

    VkImageView blurView = aoBlurImage_->View();
    info.renderPass = blurRenderPass_;
    info.pAttachments = &blurView;
    SSAO_VKCHECK(vkCreateFramebuffer(device_, &info, nullptr, &aoBlurFramebuffer_));
}

void SSAO::CreatePipelines(const Context& ctx)
{
    GraphicsPipelineConfig cfg;
    cfg.vertexBindings = {};
    cfg.vertexAttributes = {};
    cfg.cullMode = VK_CULL_MODE_NONE;
    cfg.depthTest = false;
    cfg.depthWrite = false;
    cfg.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
    cfg.colorAttachmentCount = 1;

    const auto fullscreenSpv = ReadShaderFile("shaders/pp_fullscreen.vert.spv");

    // SSAO 管线
    {
        cfg.setLayouts = {ssaoLayout_};
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 96}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/ssao.frag.spv"));
        ssaoPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), ssaoRenderPass_, std::move(v), std::move(f), cfg);
    }

    // 模糊管线（复用 pp_blur 着色器）
    {
        cfg.setLayouts = {blurLayout_};
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_blur.frag.spv"));
        blurPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), blurRenderPass_, std::move(v), std::move(f), cfg);
    }
}

void SSAO::CreateDescriptorResources(const Context& ctx)
{
    (void)ctx;
    // 采样器：线性、clamp-to-edge（GBuffer 与 AO 共用）
    VkSamplerCreateInfo samp{};
    samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp.magFilter = VK_FILTER_LINEAR;
    samp.minFilter = VK_FILTER_LINEAR;
    samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.maxLod = VK_LOD_CLAMP_NONE;
    SSAO_VKCHECK(vkCreateSampler(device_, &samp, nullptr, &sampler_));

    // SSAO 布局：binding0=position, binding1=normal
    std::array<VkDescriptorSetLayoutBinding, 2> ssaoBinds{};
    for (uint32_t b = 0; b < 2; ++b)
    {
        ssaoBinds[b].binding = b;
        ssaoBinds[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ssaoBinds[b].descriptorCount = 1;
        ssaoBinds[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        ssaoBinds[b].pImmutableSamplers = &sampler_;
    }
    VkDescriptorSetLayoutCreateInfo ssaoInfo{};
    ssaoInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ssaoInfo.bindingCount = 2;
    ssaoInfo.pBindings = ssaoBinds.data();
    SSAO_VKCHECK(vkCreateDescriptorSetLayout(device_, &ssaoInfo, nullptr, &ssaoLayout_));

    // 模糊布局：binding0=AO输入
    VkDescriptorSetLayoutBinding blurBind{};
    blurBind.binding = 0;
    blurBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    blurBind.descriptorCount = 1;
    blurBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    blurBind.pImmutableSamplers = &sampler_;
    VkDescriptorSetLayoutCreateInfo blurInfo{};
    blurInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    blurInfo.bindingCount = 1;
    blurInfo.pBindings = &blurBind;
    SSAO_VKCHECK(vkCreateDescriptorSetLayout(device_, &blurInfo, nullptr, &blurLayout_));

    // 描述符池
    std::array<VkDescriptorPoolSize, 1> poolSizes{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2;
    SSAO_VKCHECK(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descPool_));

    // 分配描述符集
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = descPool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &ssaoLayout_;
    SSAO_VKCHECK(vkAllocateDescriptorSets(device_, &alloc, &ssaoSet_));
    alloc.pSetLayouts = &blurLayout_;
    SSAO_VKCHECK(vkAllocateDescriptorSets(device_, &alloc, &blurSet_));
}

void SSAO::UpdateDescriptorSets(VkImageView positionView, VkImageView normalView)
{
    VkDescriptorImageInfo posInfo{sampler_, positionView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo normInfo{sampler_, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (uint32_t b = 0; b < 2; ++b)
    {
        writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[b].dstSet = ssaoSet_;
        writes[b].dstBinding = b;
        writes[b].dstArrayElement = 0;
        writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[b].descriptorCount = 1;
        writes[b].pImageInfo = (b == 0) ? &posInfo : &normInfo;
    }
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // 模糊集：绑定 AO 图像
    VkDescriptorImageInfo aoInfo{sampler_, aoImage_->View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet blurWrite{};
    blurWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    blurWrite.dstSet = blurSet_;
    blurWrite.dstBinding = 0;
    blurWrite.dstArrayElement = 0;
    blurWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    blurWrite.descriptorCount = 1;
    blurWrite.pImageInfo = &aoInfo;
    vkUpdateDescriptorSets(device_, 1, &blurWrite, 0, nullptr);
}

void SSAO::RecordPass(VkCommandBuffer cmd, VkImageView positionView, VkImageView normalView, const glm::mat4& viewProj,
                      const glm::vec3& cameraPos)
{
    if (!IsValid() || cmd == VK_NULL_HANDLE)
        return;

    UpdateDescriptorSets(positionView, normalView);

    // ---- SSAO Pass ----
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = ssaoRenderPass_;
    begin.framebuffer = aoFramebuffer_;
    begin.renderArea = {{0, 0}, halfExtent_};
    VkClearValue clear{.color = {{1.0f, 1.0f, 1.0f, 1.0f}}};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

    ssaoPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipeline_->GetLayout(), 0, 1, &ssaoSet_, 0,
                            nullptr);
    struct SSAOPush
    {
        glm::mat4 viewProj;
        glm::vec3 cameraPos;
        float radius;
        float bias;
        float strength;
        float pad;
    };
    SSAOPush pc{viewProj, cameraPos, radius, bias, strength, 0.0f};
    vkCmdPushConstants(cmd, ssaoPipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOPush), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // ---- 水平模糊 Pass ----
    begin.renderPass = blurRenderPass_;
    begin.framebuffer = aoBlurFramebuffer_;
    begin.renderArea = {{0, 0}, halfExtent_};
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    blurPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline_->GetLayout(), 0, 1, &blurSet_, 0,
                            nullptr);
    const glm::vec2 hDir{1.0f / static_cast<float>(halfExtent_.width), 0.0f};
    vkCmdPushConstants(cmd, blurPipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec2), &hDir);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // ---- 垂直模糊 Pass（回写到 aoImage_）----
    // 更新模糊集绑定到 aoBlurImage_
    VkDescriptorImageInfo blurInfo{sampler_, aoBlurImage_->View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = blurSet_;
    w.dstBinding = 0;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo = &blurInfo;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);

    begin.framebuffer = aoFramebuffer_;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    blurPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline_->GetLayout(), 0, 1, &blurSet_, 0,
                            nullptr);
    const glm::vec2 vDir{0.0f, 1.0f / static_cast<float>(halfExtent_.height)};
    vkCmdPushConstants(cmd, blurPipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec2), &vDir);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

VkImageView SSAO::GetAOView() const noexcept
{
    return aoImage_ ? aoImage_->View() : VK_NULL_HANDLE;
}

VkImage SSAO::GetAOImage() const noexcept
{
    return aoImage_ ? aoImage_->Get() : VK_NULL_HANDLE;
}

void SSAO::DestroyFramebuffers() noexcept
{
    if (aoFramebuffer_ != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(device_, aoFramebuffer_, nullptr);
        aoFramebuffer_ = VK_NULL_HANDLE;
    }
    if (aoBlurFramebuffer_ != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(device_, aoBlurFramebuffer_, nullptr);
        aoBlurFramebuffer_ = VK_NULL_HANDLE;
    }
}

void SSAO::DestroyPipelines() noexcept
{
    ssaoPipeline_.reset();
    blurPipeline_.reset();
}
} // namespace BigHero::Render

