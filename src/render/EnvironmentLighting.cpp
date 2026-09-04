#include "render/EnvironmentLighting.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Buffer.h"
#include "render/Context.h"
#include "render/pipeline.h"
#include "render/shader_loader.h"

#include <array>
#include <cmath>
#include <stdexcept>

namespace BigHero
{
namespace
{
// Vulkan立方图面寻址约定：dir = major + sVec*(2s-1) + tVec*(2t-1)
// （t=0为贴图首行）。CPU环境生成与GPU卷积重建共用同一张表，保证自洽
struct FaceBasis
{
    glm::vec3 major;
    glm::vec3 sVec;
    glm::vec3 tVec;
};
constexpr FaceBasis kCubeFaces[6] = {
    {glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, -1, 0)}, // +X
    {glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, -1, 0)}, // -X
    {glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1)},   // +Y
    {glm::vec3(0, -1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)}, // -Y
    {glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), glm::vec3(0, -1, 0)},  // +Z
    {glm::vec3(0, 0, -1), glm::vec3(-1, 0, 0), glm::vec3(0, -1, 0)} // -Z
};

// 卷积通道推送常量：面基矩阵 + 预滤波粗糙度
struct PushFace
{
    glm::mat4 basis;
    float roughness;
    float pad0;
    float pad1;
    float pad2;
};
static_assert(sizeof(PushFace) == 80, "PushFace必须为80字节");

void createColorRenderPass(VkDevice device, VkFormat format, VkRenderPass& outPass)
{
    VkAttachmentDescription color{};
    color.format = format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &color;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 1;
    passInfo.pDependencies = &dependency;
    VK_CHECK(vkCreateRenderPass(device, &passInfo, nullptr, &outPass), "创建IBL颜色渲染通道");
}

VkImageView createFaceView(VkDevice device, VkImage image, VkFormat format, uint32_t mipLevel, uint32_t layer)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = mipLevel;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = layer;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &view), "创建立方图面视图");
    return view;
}

[[nodiscard]] bool formatSupports(VkPhysicalDevice gpu, VkFormat format, VkFormatFeatureFlags features)
{
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(gpu, format, &props);
    return (props.optimalTilingFeatures & features) == features;
}
} // namespace

glm::vec3 EnvironmentLighting::SampleSky(glm::vec3 dir)
{
    dir = glm::normalize(dir);
    const float h = dir.y;

    // 天空渐变：地平线暖白 -> 天顶深蓝；下半球为地面反弹色
    const glm::vec3 zenith(0.10f, 0.22f, 0.55f);
    const glm::vec3 horizon(0.75f, 0.80f, 0.90f);
    const glm::vec3 ground(0.28f, 0.25f, 0.22f);
    glm::vec3 sky =
        (h > 0.0f) ? glm::mix(horizon, zenith, std::pow(h, 0.45f)) : glm::mix(horizon, ground, std::pow(-h, 0.35f));

    // 太阳：方向与场景方向光一致（lightDir取反）
    const glm::vec3 sunDir = glm::normalize(glm::vec3(-0.5f, 1.0f, 0.35f));
    const float cosSun = glm::dot(dir, sunDir);
    const float sunDisc = glm::smoothstep(0.9992f, 0.9997f, cosSun) * 60.0f;
    const float sunGlow =
        std::pow(glm::max(cosSun, 0.0f), 200.0f) * 3.0f + std::pow(glm::max(cosSun, 0.0f), 16.0f) * 0.35f;
    sky += (sunDisc + sunGlow) * glm::vec3(1.0f, 0.92f, 0.75f);

    return sky;
}

void EnvironmentLighting::Create(const Context& ctx)
{
    using namespace Render;
    Destroy();
    ctx_ = &ctx;
    const VkDevice device = ctx.Device();

    // ---- 目标格式支持检查 ----
    if (!formatSupports(ctx.PhysicalDevice(), VK_FORMAT_R16G16B16A16_SFLOAT,
                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        throw std::runtime_error("EnvironmentLighting: 设备不支持RGBA16F颜色附件");
    if (!formatSupports(ctx.PhysicalDevice(), VK_FORMAT_R16G16_SFLOAT,
                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        throw std::runtime_error("EnvironmentLighting: 设备不支持RG16F颜色附件");

    // ---- 共享采样器 ----
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(kPrefilterMips);
    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &sampler_), "创建IBL采样器");

    // ---- 环境立方图：CPU程序化生成并上传 ----
    envCubemap_.Create(ctx, kEnvSize, kEnvSize, VK_FORMAT_R32G32B32A32_SFLOAT,
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_SAMPLE_COUNT_1_BIT, 6,
                       VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);

    {
        std::vector<glm::vec4> pixels(static_cast<size_t>(kEnvSize) * kEnvSize * 6);
        for (uint32_t face = 0; face < 6; ++face)
        {
            const FaceBasis& fb = kCubeFaces[face];
            for (uint32_t y = 0; y < kEnvSize; ++y)
            {
                for (uint32_t x = 0; x < kEnvSize; ++x)
                {
                    const float s = (static_cast<float>(x) + 0.5f) / static_cast<float>(kEnvSize);
                    const float t = (static_cast<float>(y) + 0.5f) / static_cast<float>(kEnvSize);
                    const glm::vec3 dir =
                        glm::normalize(fb.major + fb.sVec * (2.0f * s - 1.0f) + fb.tVec * (2.0f * t - 1.0f));
                    const glm::vec3 color = SampleSky(dir);
                    const size_t index = (static_cast<size_t>(face) * kEnvSize + y) * kEnvSize + x;
                    pixels[index] = glm::vec4(color, 1.0f);
                }
            }
        }

        Buffer staging;
        staging.Create(ctx, static_cast<VkDeviceSize>(pixels.size() * sizeof(glm::vec4)),
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.UploadData(ctx, pixels.data(), static_cast<VkDeviceSize>(pixels.size() * sizeof(glm::vec4)));

        envCubemap_.TransitionLayout(ctx, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6);
        envCubemap_.CopyFromBuffer(ctx, staging.Get(), 6);
        envCubemap_.TransitionLayout(ctx, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6);
    }

    // ---- 渲染目标 ----
    createColorImage(ctx, irradianceCubemap_, kIrradianceSize, VK_FORMAT_R16G16B16A16_SFLOAT, 1, 6);
    createColorImage(ctx, prefilteredCubemap_, kPrefilterSize, VK_FORMAT_R16G16B16A16_SFLOAT, kPrefilterMips, 6);
    createColorImage(ctx, brdfLut_, kBrdfSize, VK_FORMAT_R16G16_SFLOAT, 1, 1);

    createRenderPasses(ctx);

    // ---- IBL描述符（卷积管线采样环境立方图） ----
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &envSetLayout_), "创建IBL描述符布局");

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &envDescriptorPool_), "创建IBL描述符池");

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = envDescriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &envSetLayout_;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &envSet_), "分配IBL描述符集");

        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = sampler_;
        imageInfo.imageView = envCubemap_.View();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = envSet_;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // ---- 帧缓冲：辐照度每面 / 预滤波每级每面 / BRDF LUT ----
    const auto makeFaceFb = [&](VkImageView attachment, VkRenderPass pass, uint32_t size)
    {
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = pass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &attachment;
        fbInfo.width = size;
        fbInfo.height = size;
        fbInfo.layers = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        VK_CHECK(vkCreateFramebuffer(device, &fbInfo, nullptr, &fb), "创建IBL帧缓冲");
        return fb;
    };

    for (uint32_t face = 0; face < 6; ++face)
    {
        const VkImageView view =
            createFaceView(device, irradianceCubemap_.Get(), VK_FORMAT_R16G16B16A16_SFLOAT, 0, face);
        irradianceFramebuffers_.push_back(makeFaceFb(view, cubeColorPass_, kIrradianceSize));
        vkDestroyImageView(device, view, nullptr);
    }

    for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
    {
        const uint32_t mipSize = kPrefilterSize >> mip;
        for (uint32_t face = 0; face < 6; ++face)
        {
            const VkImageView view =
                createFaceView(device, prefilteredCubemap_.Get(), VK_FORMAT_R16G16B16A16_SFLOAT, mip, face);
            prefilterFaceViews_.push_back(view);
            prefilterFramebuffers_.push_back(makeFaceFb(view, cubeColorPass_, mipSize));
        }
    }

    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = brdfLut_.Get();
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &brdfView_), "创建BRDF LUT视图");
        brdfFramebuffer_ = makeFaceFb(brdfView_, brdfColorPass_, kBrdfSize);
    }

    // ---- 卷积管线 ----
    const auto makeConvPipeline = [&](const char* fragPath)
    {
        auto vert = ShaderModuleHandle(device, ReadShaderFile("shaders/env_conv.vert.spv"));
        auto frag = ShaderModuleHandle(device, ReadShaderFile(fragPath));
        GraphicsPipelineConfig config;
        config.setLayouts = {envSetLayout_};
        config.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushFace)}};
        config.depthTest = false;
        config.depthWrite = false;
        config.cullMode = VK_CULL_MODE_NONE;
        return GraphicsPipeline(device, cubeColorPass_, std::move(vert), std::move(frag), config);
    };
    auto irradiancePipe = makeConvPipeline("shaders/irradiance.frag.spv");
    auto prefilterPipe = makeConvPipeline("shaders/prefilter.frag.spv");

    auto brdfVert = ShaderModuleHandle(device, ReadShaderFile("shaders/env_conv.vert.spv"));
    auto brdfFrag = ShaderModuleHandle(device, ReadShaderFile("shaders/brdf_lut.frag.spv"));
    GraphicsPipelineConfig brdfConfig;
    brdfConfig.setLayouts = {envSetLayout_};
    brdfConfig.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushFace)}};
    brdfConfig.depthTest = false;
    brdfConfig.depthWrite = false;
    brdfConfig.cullMode = VK_CULL_MODE_NONE;
    GraphicsPipeline brdfPipe(device, brdfColorPass_, std::move(brdfVert), std::move(brdfFrag), brdfConfig);

    // ---- GPU一次性预计算：辐照度卷积 -> 预滤波mip链 -> BRDF LUT ----
    ctx.SubmitOneTime(
        [&](VkCommandBuffer cmd)
        {
            const auto faceBasisMat = [](uint32_t face)
            {
                glm::mat4 basis(1.0f);
                basis[0] = glm::vec4(kCubeFaces[face].sVec, 0.0f);
                basis[1] = glm::vec4(kCubeFaces[face].tVec, 0.0f);
                basis[2] = glm::vec4(kCubeFaces[face].major, 0.0f);
                return basis;
            };

            // 辐照度卷积
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, irradiancePipe.GetPipeline());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, irradiancePipe.GetLayout(), 0, 1, &envSet_, 0,
                                    nullptr);
            for (uint32_t face = 0; face < 6; ++face)
            {
                VkRenderPassBeginInfo passInfo{};
                passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                passInfo.renderPass = cubeColorPass_;
                passInfo.framebuffer = irradianceFramebuffers_[face];
                passInfo.renderArea.extent = {kIrradianceSize, kIrradianceSize};
                vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport viewport{};
                viewport.width = static_cast<float>(kIrradianceSize);
                viewport.height = static_cast<float>(kIrradianceSize);
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor{{0, 0}, {kIrradianceSize, kIrradianceSize}};
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                const PushFace push{faceBasisMat(face), 0.0f, 0.0f, 0.0f, 0.0f};
                vkCmdPushConstants(cmd, irradiancePipe.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushFace),
                                   &push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
                vkCmdEndRenderPass(cmd);
            }

            // 预滤波镜面：每级mip对应粗糙度
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prefilterPipe.GetPipeline());
            for (uint32_t mip = 0; mip < kPrefilterMips; ++mip)
            {
                const uint32_t mipSize = kPrefilterSize >> mip;
                const float roughness = static_cast<float>(mip) / static_cast<float>(kPrefilterMips - 1);
                for (uint32_t face = 0; face < 6; ++face)
                {
                    VkRenderPassBeginInfo passInfo{};
                    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    passInfo.renderPass = cubeColorPass_;
                    passInfo.framebuffer = prefilterFramebuffers_[mip * 6 + face];
                    passInfo.renderArea.extent = {mipSize, mipSize};
                    vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

                    VkViewport viewport{};
                    viewport.width = static_cast<float>(mipSize);
                    viewport.height = static_cast<float>(mipSize);
                    viewport.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &viewport);
                    VkRect2D scissor{{0, 0}, {mipSize, mipSize}};
                    vkCmdSetScissor(cmd, 0, 1, &scissor);

                    const PushFace push{faceBasisMat(face), roughness, 0.0f, 0.0f, 0.0f};
                    vkCmdPushConstants(cmd, prefilterPipe.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushFace),
                                       &push);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);
                }
            }

            // BRDF LUT
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, brdfPipe.GetPipeline());
            VkRenderPassBeginInfo brdfPass{};
            brdfPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            brdfPass.renderPass = brdfColorPass_;
            brdfPass.framebuffer = brdfFramebuffer_;
            brdfPass.renderArea.extent = {kBrdfSize, kBrdfSize};
            vkCmdBeginRenderPass(cmd, &brdfPass, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.width = static_cast<float>(kBrdfSize);
            viewport.height = static_cast<float>(kBrdfSize);
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{{0, 0}, {kBrdfSize, kBrdfSize}};
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            const PushFace push{glm::mat4(1.0f), 0.0f, 0.0f, 0.0f, 0.0f};
            vkCmdPushConstants(cmd, brdfPipe.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushFace), &push);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
        });

    // 卷积管线随作用域RAII释放，仅深度预计算资源保留
    destroyGenerationResources();
    LOG_INFO("IBL环境光照预计算完成（辐照度/预滤波/BRDF LUT）");
}

void EnvironmentLighting::createColorImage(const Context& ctx, Image& image, uint32_t size, VkFormat format,
                                           uint32_t mipLevels, uint32_t layers)
{
    image.Create(ctx, size, size, format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, VK_SAMPLE_COUNT_1_BIT,
                 layers, layers > 1 ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
                 layers > 1 ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D);
}

void EnvironmentLighting::createRenderPasses(const Context& ctx)
{
    createColorRenderPass(ctx.Device(), VK_FORMAT_R16G16B16A16_SFLOAT, cubeColorPass_);
    createColorRenderPass(ctx.Device(), VK_FORMAT_R16G16_SFLOAT, brdfColorPass_);
}

void EnvironmentLighting::destroyGenerationResources()
{
    const VkDevice device = ctx_->Device();
    for (VkFramebuffer fb : irradianceFramebuffers_)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, fb, nullptr);
    irradianceFramebuffers_.clear();
    for (VkImageView view : prefilterFaceViews_)
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
    prefilterFaceViews_.clear();
    for (VkFramebuffer fb : prefilterFramebuffers_)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device, fb, nullptr);
    prefilterFramebuffers_.clear();
    if (brdfFramebuffer_ != VK_NULL_HANDLE)
    {
        vkDestroyFramebuffer(device, brdfFramebuffer_, nullptr);
        brdfFramebuffer_ = VK_NULL_HANDLE;
    }
    if (brdfView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, brdfView_, nullptr);
        brdfView_ = VK_NULL_HANDLE;
    }
    if (envDescriptorPool_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, envDescriptorPool_, nullptr);
        envDescriptorPool_ = VK_NULL_HANDLE;
        envSet_ = VK_NULL_HANDLE;
    }
    if (envSetLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, envSetLayout_, nullptr);
        envSetLayout_ = VK_NULL_HANDLE;
    }
    if (cubeColorPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device, cubeColorPass_, nullptr);
        cubeColorPass_ = VK_NULL_HANDLE;
    }
    if (brdfColorPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device, brdfColorPass_, nullptr);
        brdfColorPass_ = VK_NULL_HANDLE;
    }
}

void EnvironmentLighting::Destroy()
{
    if (ctx_ == nullptr)
        return;

    const VkDevice device = ctx_->Device();
    destroyGenerationResources();

    if (sampler_ != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    envCubemap_.Destroy();
    irradianceCubemap_.Destroy();
    prefilteredCubemap_.Destroy();
    brdfLut_.Destroy();
    ctx_ = nullptr;
}
} // namespace BigHero

