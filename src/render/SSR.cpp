#include "render/SSR.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Context.h"
#include "render/image.h"
#include "render/pipeline.h"
#include "render/shader_loader.h"
#include <algorithm>
#include <array>

#define SSR_VKCHECK(result) VK_CHECK(result, "SSR")

namespace BigHero::Render
{
SSR::~SSR()
{
    Destroy();
}

SSR::SSR(SSR&& other) noexcept
{
    device_ = other.device_;
    extent_ = other.extent_;
    halfExtent_ = other.halfExtent_;
    reflectionImage_ = std::move(other.reflectionImage_);
    reflectionBlurImage_ = std::move(other.reflectionBlurImage_);
    rayRenderPass_ = other.rayRenderPass_;
    blurRenderPass_ = other.blurRenderPass_;
    rayFramebuffer_ = other.rayFramebuffer_;
    blurFramebuffer_ = other.blurFramebuffer_;
    rayPipeline_ = std::move(other.rayPipeline_);
    blurPipeline_ = std::move(other.blurPipeline_);
    rayLayout_ = other.rayLayout_;
    blurLayout_ = other.blurLayout_;
    descPool_ = other.descPool_;
    raySet_ = other.raySet_;
    blurSet_ = other.blurSet_;
    sampler_ = other.sampler_;
    maxDistance = other.maxDistance;
    stepCount = other.stepCount;
    thickness = other.thickness;
    edgeFade = other.edgeFade;
    other.device_ = VK_NULL_HANDLE;
    other.rayRenderPass_ = VK_NULL_HANDLE;
    other.blurRenderPass_ = VK_NULL_HANDLE;
    other.rayFramebuffer_ = VK_NULL_HANDLE;
    other.blurFramebuffer_ = VK_NULL_HANDLE;
    other.rayLayout_ = VK_NULL_HANDLE;
    other.blurLayout_ = VK_NULL_HANDLE;
    other.descPool_ = VK_NULL_HANDLE;
    other.raySet_ = VK_NULL_HANDLE;
    other.blurSet_ = VK_NULL_HANDLE;
    other.sampler_ = VK_NULL_HANDLE;
}

SSR& SSR::operator=(SSR&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        device_ = other.device_;
        extent_ = other.extent_;
        halfExtent_ = other.halfExtent_;
        reflectionImage_ = std::move(other.reflectionImage_);
        reflectionBlurImage_ = std::move(other.reflectionBlurImage_);
        rayRenderPass_ = other.rayRenderPass_;
        blurRenderPass_ = other.blurRenderPass_;
        rayFramebuffer_ = other.rayFramebuffer_;
        blurFramebuffer_ = other.blurFramebuffer_;
        rayPipeline_ = std::move(other.rayPipeline_);
        blurPipeline_ = std::move(other.blurPipeline_);
        rayLayout_ = other.rayLayout_;
        blurLayout_ = other.blurLayout_;
        descPool_ = other.descPool_;
        raySet_ = other.raySet_;
        blurSet_ = other.blurSet_;
        sampler_ = other.sampler_;
        maxDistance = other.maxDistance;
        stepCount = other.stepCount;
        thickness = other.thickness;
        edgeFade = other.edgeFade;
        other.device_ = VK_NULL_HANDLE;
        other.rayRenderPass_ = VK_NULL_HANDLE;
        other.blurRenderPass_ = VK_NULL_HANDLE;
        other.rayFramebuffer_ = VK_NULL_HANDLE;
        other.blurFramebuffer_ = VK_NULL_HANDLE;
        other.rayLayout_ = VK_NULL_HANDLE;
        other.blurLayout_ = VK_NULL_HANDLE;
        other.descPool_ = VK_NULL_HANDLE;
        other.raySet_ = VK_NULL_HANDLE;
        other.blurSet_ = VK_NULL_HANDLE;
        other.sampler_ = VK_NULL_HANDLE;
    }
    return *this;
}

void SSR::Init(const Context& ctx, VkExtent2D extent)
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

void SSR::Destroy() noexcept
{
    if (device_ == VK_NULL_HANDLE)
        return;
    DestroyPipelines();
    DestroyFramebuffers();
    if (rayRenderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, rayRenderPass_, nullptr);
        rayRenderPass_ = VK_NULL_HANDLE;
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
    if (rayLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device_, rayLayout_, nullptr);
        rayLayout_ = VK_NULL_HANDLE;
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
    reflectionImage_.reset();
    reflectionBlurImage_.reset();
    device_ = VK_NULL_HANDLE;
}

void SSR::Recreate(const Context& ctx, VkExtent2D extent)
{
    DestroyFramebuffers();
    DestroyPipelines();
    reflectionImage_.reset();
    reflectionBlurImage_.reset();
    extent_ = extent;
    halfExtent_ = {std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)};
    CreateImages(ctx);
    CreateFramebuffers();
    CreatePipelines(ctx);
}

void SSR::CreateImages(const Context& ctx)
{
    reflectionImage_ = std::make_unique<BigHero::Image>();
    reflectionImage_->Create(ctx, halfExtent_.width, halfExtent_.height, VK_FORMAT_R16G16B16A16_SFLOAT,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    reflectionBlurImage_ = std::make_unique<BigHero::Image>();
    reflectionBlurImage_->Create(ctx, halfExtent_.width, halfExtent_.height, VK_FORMAT_R16G16B16A16_SFLOAT,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
}

void SSR::CreateRenderPasses(const Context& ctx)
{
    (void)ctx;
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
    SSR_VKCHECK(vkCreateRenderPass(device_, &info, nullptr, &rayRenderPass_));
    SSR_VKCHECK(vkCreateRenderPass(device_, &info, nullptr, &blurRenderPass_));
}

void SSR::CreateFramebuffers()
{
    VkImageView reflView = reflectionImage_->View();
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = rayRenderPass_;
    info.attachmentCount = 1;
    info.pAttachments = &reflView;
    info.width = halfExtent_.width;
    info.height = halfExtent_.height;
    info.layers = 1;
    SSR_VKCHECK(vkCreateFramebuffer(device_, &info, nullptr, &rayFramebuffer_));

    VkImageView blurView = reflectionBlurImage_->View();
    info.renderPass = blurRenderPass_;
    info.pAttachments = &blurView;
    SSR_VKCHECK(vkCreateFramebuffer(device_, &info, nullptr, &blurFramebuffer_));
}

void SSR::CreatePipelines(const Context& ctx)
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

    // SSR ray march 管线
    {
        cfg.setLayouts = {rayLayout_};
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 104}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/ssr_ray.frag.spv"));
        rayPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), rayRenderPass_, std::move(v), std::move(f), cfg);
    }

    // SSR 模糊管线
    {
        cfg.setLayouts = {blurLayout_};
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/ssr_blur.frag.spv"));
        blurPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), blurRenderPass_, std::move(v), std::move(f), cfg);
    }
}

void SSR::CreateDescriptorResources(const Context& ctx)
{
    (void)ctx;
    VkSamplerCreateInfo samp{};
    samp.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp.magFilter = VK_FILTER_LINEAR;
    samp.minFilter = VK_FILTER_LINEAR;
    samp.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp.maxLod = VK_LOD_CLAMP_NONE;
    SSR_VKCHECK(vkCreateSampler(device_, &samp, nullptr, &sampler_));

    // Ray 布局：binding0=position, binding1=normal, binding2=sceneColor
    std::array<VkDescriptorSetLayoutBinding, 3> rayBinds{};
    for (uint32_t b = 0; b < 3; ++b)
    {
        rayBinds[b].binding = b;
        rayBinds[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        rayBinds[b].descriptorCount = 1;
        rayBinds[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        rayBinds[b].pImmutableSamplers = &sampler_;
    }
    VkDescriptorSetLayoutCreateInfo rayInfo{};
    rayInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    rayInfo.bindingCount = 3;
    rayInfo.pBindings = rayBinds.data();
    SSR_VKCHECK(vkCreateDescriptorSetLayout(device_, &rayInfo, nullptr, &rayLayout_));

    // 模糊布局：binding0=输入
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
    SSR_VKCHECK(vkCreateDescriptorSetLayout(device_, &blurInfo, nullptr, &blurLayout_));

    std::array<VkDescriptorPoolSize, 1> poolSizes{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5}};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2;
    SSR_VKCHECK(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descPool_));

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = descPool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &rayLayout_;
    SSR_VKCHECK(vkAllocateDescriptorSets(device_, &alloc, &raySet_));
    alloc.pSetLayouts = &blurLayout_;
    SSR_VKCHECK(vkAllocateDescriptorSets(device_, &alloc, &blurSet_));
}

void SSR::UpdateDescriptorSets(VkImageView positionView, VkImageView normalView, VkImageView sceneColorView)
{
    VkDescriptorImageInfo posInfo{sampler_, positionView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo normInfo{sampler_, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo colorInfo{sampler_, sceneColorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (uint32_t b = 0; b < 3; ++b)
    {
        writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[b].dstSet = raySet_;
        writes[b].dstBinding = b;
        writes[b].dstArrayElement = 0;
        writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[b].descriptorCount = 1;
    }
    writes[0].pImageInfo = &posInfo;
    writes[1].pImageInfo = &normInfo;
    writes[2].pImageInfo = &colorInfo;
    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void SSR::RecordPass(VkCommandBuffer cmd, VkImageView positionView, VkImageView normalView, VkImageView sceneColorView,
                     const glm::mat4& viewProj, const glm::vec3& cameraPos)
{
    if (!IsValid() || cmd == VK_NULL_HANDLE)
        return;

    UpdateDescriptorSets(positionView, normalView, sceneColorView);

    // ---- SSR Ray March Pass ----
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = rayRenderPass_;
    begin.framebuffer = rayFramebuffer_;
    begin.renderArea = {{0, 0}, halfExtent_};
    VkClearValue clear{.color = {{0.0f, 0.0f, 0.0f, 0.0f}}};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

    rayPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, rayPipeline_->GetLayout(), 0, 1, &raySet_, 0,
                            nullptr);
    struct SSRPush
    {
        glm::mat4 viewProj;
        glm::vec3 cameraPos;
        float maxDistance;
        float stepSize;
        float thickness;
        float edgeFade;
        int stepCount;
        float pad0;
        float pad1;
    };
    SSRPush pc{viewProj,  cameraPos, maxDistance, maxDistance / static_cast<float>(stepCount), thickness, edgeFade,
               stepCount, 0.0f,      0.0f};
    vkCmdPushConstants(cmd, rayPipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSRPush), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // ---- 水平模糊 Pass ----
    VkDescriptorImageInfo reflInfo{sampler_, reflectionImage_->View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = blurSet_;
    w.dstBinding = 0;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.descriptorCount = 1;
    w.pImageInfo = &reflInfo;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);

    begin.renderPass = blurRenderPass_;
    begin.framebuffer = blurFramebuffer_;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    blurPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline_->GetLayout(), 0, 1, &blurSet_, 0,
                            nullptr);
    struct BlurPush
    {
        glm::vec2 texelSize;
        int direction;
        float pad;
    };
    BlurPush hBlur{{1.0f / static_cast<float>(halfExtent_.width), 0.0f}, 0, 0.0f};
    vkCmdPushConstants(cmd, blurPipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BlurPush), &hBlur);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // ---- 垂直模糊 Pass（回写到 reflectionImage_）----
    VkDescriptorImageInfo blurInfo{sampler_, reflectionBlurImage_->View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    w.pImageInfo = &blurInfo;
    vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);

    begin.framebuffer = rayFramebuffer_;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    blurPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline_->GetLayout(), 0, 1, &blurSet_, 0,
                            nullptr);
    BlurPush vBlur{{0.0f, 1.0f / static_cast<float>(halfExtent_.height)}, 1, 0.0f};
    vkCmdPushConstants(cmd, blurPipeline_->GetLayout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(BlurPush), &vBlur);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

VkImageView SSR::GetReflectionView() const noexcept
{
    return reflectionImage_ ? reflectionImage_->View() : VK_NULL_HANDLE;
}

void SSR::DestroyFramebuffers() noexcept
{
    if (rayFramebuffer_ != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(device_, rayFramebuffer_, nullptr);
        rayFramebuffer_ = VK_NULL_HANDLE;
    }
    if (blurFramebuffer_ != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(device_, blurFramebuffer_, nullptr);
        blurFramebuffer_ = VK_NULL_HANDLE;
    }
}

void SSR::DestroyPipelines() noexcept
{
    rayPipeline_.reset();
    blurPipeline_.reset();
}
} // namespace BigHero::Render
