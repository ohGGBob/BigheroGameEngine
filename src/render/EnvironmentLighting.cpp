#include "render/EnvironmentLighting.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Buffer.h"
#include "render/Context.h"
#include "render/pipeline.h"
#include "render/shader_loader.h"
#include <vulkan/vulkan.h>

#include <stb_image.h>

#include "render/Texture.h"

#include <glm/gtc/packing.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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

uint32_t IblMipLevels()
{
    return 5u; // = EnvironmentLighting::kPrefilterMips
}

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

    // ---- 目标格式支持检查 ----
    if (!formatSupports(ctx.PhysicalDevice(), VK_FORMAT_R16G16B16A16_SFLOAT,
                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        throw std::runtime_error("EnvironmentLighting: 设备不支持RGBA16F颜色附件");
    if (!formatSupports(ctx.PhysicalDevice(), VK_FORMAT_R16G16_SFLOAT,
                        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        throw std::runtime_error("EnvironmentLighting: 设备不支持RG16F颜色附件");

    // ---- 共享采样器 ----
    createSampler(ctx);

    // ---- 环境立方图：CPU程序化生成并上传（RGBA16F：线性过滤 universally 支持，
    //      且避开 AMD 对 32F 纹理采样的驱动雷区；kPrefilterMips 级 mip 供预滤波 lod 采样） ----
    envCubemap_.Create(ctx, kEnvSize, kEnvSize, VK_FORMAT_R16G16B16A16_SFLOAT,
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT, IblMipLevels(),
                       VK_SAMPLE_COUNT_1_BIT, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);

    {
        // CPU 生成线性浮点天空后转半精度（vkCmdCopyBufferToImage 的缓冲布局须与图像纹素格式一致）
        std::vector<uint16_t> pixels(static_cast<size_t>(kEnvSize) * kEnvSize * 6 * 4);
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
                    const size_t index = ((static_cast<size_t>(face) * kEnvSize + y) * kEnvSize + x) * 4;
                    pixels[index + 0] = glm::packHalf1x16(color.r);
                    pixels[index + 1] = glm::packHalf1x16(color.g);
                    pixels[index + 2] = glm::packHalf1x16(color.b);
                    pixels[index + 3] = glm::packHalf1x16(1.0f);
                }
            }
        }

        Buffer staging;
        staging.Create(ctx, static_cast<VkDeviceSize>(pixels.size() * sizeof(uint16_t)),
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.UploadData(ctx, pixels.data(), static_cast<VkDeviceSize>(pixels.size() * sizeof(uint16_t)));

        envCubemap_.TransitionLayout(ctx, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6);
        envCubemap_.CopyFromBuffer(ctx, staging.Get(), 6);
        envCubemap_.TransitionLayout(ctx, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 6);
    }

    setupIBL(ctx);
}

void EnvironmentLighting::setupIBL(const Context& ctx)
{
    using namespace Render;
    const VkDevice device = ctx.Device();

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
        // 视图必须保活：帧缓冲不持有附件视图引用，销毁后 begin 引用失效句柄 → GPU 页错误
        // （与下方 prefilterFaceViews_ 的保留模式一致）
        irradianceFaceViews_.push_back(view);
        irradianceFramebuffers_.push_back(makeFaceFb(view, cubeColorPass_, kIrradianceSize));
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
        // 片段着色器（prefilter）读取 pushFace.roughness，范围必须同时覆盖两个阶段
        config.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                                    sizeof(PushFace)}};
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
    // 与卷积管线一致：范围覆盖顶点+片段（brdf_lut 片段若读 push 常量也需覆盖）
    brdfConfig.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                                    sizeof(PushFace)}};
    brdfConfig.depthTest = false;
    brdfConfig.depthWrite = false;
    brdfConfig.cullMode = VK_CULL_MODE_NONE;
    GraphicsPipeline brdfPipe(device, brdfColorPass_, std::move(brdfVert), std::move(brdfFrag), brdfConfig);

    // ---- GPU一次性预计算：辐照度卷积 -> 预滤波mip链 -> BRDF LUT ----

    const auto faceBasisMat = [](uint32_t face)
    {
        glm::mat4 basis(1.0f);
        basis[0] = glm::vec4(kCubeFaces[face].sVec, 0.0f);
        basis[1] = glm::vec4(kCubeFaces[face].tVec, 0.0f);
        basis[2] = glm::vec4(kCubeFaces[face].major, 0.0f);
        return basis;
    };

    // 0. 环境立方图 mip 链生成（kPrefilterMips 级，双路径统一：
    //    程序化路径经 buffer 上传 mip0、HDR 路径经渲染通道写 mip0，此处均处于 SHADER_READ_ONLY）
    if (const uint32_t envMips = IblMipLevels(); envMips > 1)
        ctx.SubmitOneTime(
            [&](VkCommandBuffer cmd)
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.image = envCubemap_.Get();
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 6;

                const auto transition = [&](uint32_t baseMip, uint32_t levelCount, VkImageLayout oldLayout,
                                            VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
                {
                    barrier.subresourceRange.baseMipLevel = baseMip;
                    barrier.subresourceRange.levelCount = levelCount;
                    barrier.oldLayout = oldLayout;
                    barrier.newLayout = newLayout;
                    barrier.srcAccessMask = srcAccess;
                    barrier.dstAccessMask = dstAccess;
                    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                };

                // mip0 -> 传输源（两种路径都停在 SHADER_READ_ONLY）
                transition(0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT);
                // mip 1..N-1 -> 传输目标（内容本就未定义/未写入，按 UNDEFINED 进入即可）
                transition(1, envMips - 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

                for (uint32_t level = 1; level < envMips; ++level)
                {
                    const int32_t srcDim = static_cast<int32_t>(kEnvSize >> (level - 1));
                    const int32_t dstDim = static_cast<int32_t>(kEnvSize >> level);
                    VkImageBlit blit{};
                    blit.srcOffsets[1] = {srcDim, srcDim, 1};
                    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 6};
                    blit.dstOffsets[1] = {dstDim, dstDim, 1};
                    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 6};
                    vkCmdBlitImage(cmd, envCubemap_.Get(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, envCubemap_.Get(),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

                    // 本级写完转传输源，供下一级降采样
                    transition(level, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_PIPELINE_STAGE_TRANSFER_BIT);
                }

                // 全链（此时均为 TRANSFER_SRC）回着色器只读，供卷积描述符采样
                transition(0, envMips, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            });

    // 1. 辐照度卷积（6面，独立提交以降低单命令缓冲复杂度）
    ctx.SubmitOneTime(
        [&](VkCommandBuffer cmd)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, irradiancePipe.GetPipeline());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, irradiancePipe.GetLayout(), 0, 1, &envSet_, 0,
                                    nullptr);
            // 通道声明 LOAD_OP_CLEAR，begin 必须提供清除值（缺失即规范违规，
            // AMD 驱动会解引用无效清除值数组 → 内核页错误 → GPU 复位 141）
            VkClearValue clearValue{};
            clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            for (uint32_t face = 0; face < 6; ++face)
            {
                VkRenderPassBeginInfo passInfo{};
                passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                passInfo.renderPass = cubeColorPass_;
                passInfo.framebuffer = irradianceFramebuffers_[face];
                passInfo.renderArea.extent = {kIrradianceSize, kIrradianceSize};
                passInfo.clearValueCount = 1;
                passInfo.pClearValues = &clearValue;
                vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport viewport{};
                viewport.width = static_cast<float>(kIrradianceSize);
                viewport.height = static_cast<float>(kIrradianceSize);
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd, 0, 1, &viewport);
                VkRect2D scissor{{0, 0}, {kIrradianceSize, kIrradianceSize}};
                vkCmdSetScissor(cmd, 0, 1, &scissor);

                const PushFace push{faceBasisMat(face), 0.0f, 0.0f, 0.0f, 0.0f};
                // 布局声明 V|F 范围（prefilter 片段读 roughness），调用 stageFlags 必须与之
                // 一致（VUID-vkCmdPushConstants-offset-01796）；片段不读时多推阶段合法
                vkCmdPushConstants(cmd, irradiancePipe.GetLayout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushFace),
                                   &push);
                vkCmdDraw(cmd, 3, 1, 0, 0);
                vkCmdEndRenderPass(cmd);
            }
        });

    // 2. 预滤波镜面（5 mip x 6面，独立提交）
    ctx.SubmitOneTime(
        [&](VkCommandBuffer cmd)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prefilterPipe.GetPipeline());
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, prefilterPipe.GetLayout(), 0, 1, &envSet_, 0,
                                    nullptr);
            VkClearValue clearValue{};
            clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
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
                    passInfo.clearValueCount = 1;
                    passInfo.pClearValues = &clearValue;
                    vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

                    VkViewport viewport{};
                    viewport.width = static_cast<float>(mipSize);
                    viewport.height = static_cast<float>(mipSize);
                    viewport.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &viewport);
                    VkRect2D scissor{{0, 0}, {mipSize, mipSize}};
                    vkCmdSetScissor(cmd, 0, 1, &scissor);

                    const PushFace push{faceBasisMat(face), roughness, 0.0f, 0.0f, 0.0f};
                    // 片段着色器读取 pushFace.roughness，stageFlags 必须覆盖 V|F（与布局一致）
                    vkCmdPushConstants(cmd, prefilterPipe.GetLayout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushFace),
                                       &push);
                    vkCmdDraw(cmd, 3, 1, 0, 0);
                    vkCmdEndRenderPass(cmd);
                }
            }
        });

    // 3. BRDF LUT（512x512，独立提交）
    ctx.SubmitOneTime(
        [&](VkCommandBuffer cmd)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, brdfPipe.GetPipeline());
            VkRenderPassBeginInfo brdfPass{};
            brdfPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            brdfPass.renderPass = brdfColorPass_;
            brdfPass.framebuffer = brdfFramebuffer_;
            brdfPass.renderArea.extent = {kBrdfSize, kBrdfSize};
            VkClearValue clearValue{};
            clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            brdfPass.clearValueCount = 1;
            brdfPass.pClearValues = &clearValue;
            vkCmdBeginRenderPass(cmd, &brdfPass, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.width = static_cast<float>(kBrdfSize);
            viewport.height = static_cast<float>(kBrdfSize);
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor{{0, 0}, {kBrdfSize, kBrdfSize}};
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            const PushFace push{glm::mat4(1.0f), 0.0f, 0.0f, 0.0f, 0.0f};
            // brdfConfig 布局同样声明 V|F 范围，调用处对齐（VUID-vkCmdPushConstants-offset-01796）
            vkCmdPushConstants(cmd, brdfPipe.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(PushFace), &push);
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
    // 幂等保护：CreateFromFile 的立方图转换期已创建过（句柄沿用），避免重复创建泄漏
    if (cubeColorPass_ != VK_NULL_HANDLE)
        return;
    createColorRenderPass(ctx.Device(), VK_FORMAT_R16G16B16A16_SFLOAT, cubeColorPass_);
    createColorRenderPass(ctx.Device(), VK_FORMAT_R16G16_SFLOAT, brdfColorPass_);
}

void EnvironmentLighting::destroyGenerationResources()
{
    const VkDevice device = ctx_->Device();
    for (VkImageView view : irradianceFaceViews_)
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
    irradianceFaceViews_.clear();
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

bool EnvironmentLighting::CreateFromFile(const Context& ctx, const std::string& filePath)
{
    ctx_ = &ctx;

    // 首先尝试加载HDR文件
    if (!LoadHDRTexture(filePath))
    {
        LOG_ERROR("Failed to load HDR texture from file: " + filePath);
        return false;
    }

    // 创建环境映射采样器
    createSampler(*ctx_);

    // 完整IBL预计算（辐照度/预滤波/BRDF LUT），与程序化Create()路径共用
    setupIBL(*ctx_);

    hdrLoaded_ = true;
    LOG_INFO("Successfully loaded HDR environment lighting from file");
    return true;
}

bool EnvironmentLighting::LoadHDRTexture(const std::string& filePath)
{
    // 使用stb_image加载HDR文件
    int width, height, channels;
    float* pixels = stbi_loadf(filePath.c_str(), &width, &height, &channels, 4);

    if (!pixels)
    {
        LOG_ERROR("Failed to load HDR file");
        return false;
    }

    if (width <= 0 || height <= 0)
    {
        LOG_ERROR("Invalid HDR dimensions");
        stbi_image_free(pixels);
        return false;
    }

    LOG_INFO("Loaded HDR texture");

    // 将HDR纹理转换为立方图
    if (!CreateCubemapFromHDR(width, height, pixels))
    {
        LOG_ERROR("Failed to create cubemap from HDR");
        stbi_image_free(pixels);
        return false;
    }

    stbi_image_free(pixels);
    return true;
}

bool EnvironmentLighting::CreateCubemapFromHDR(int width, int height, void* pixels)
{
    // 直接从浮点像素创建等距柱状HDR纹理（保留高光范围，不经过临时文件+8-bit重读）
    Texture tempTexture;
    tempTexture.CreateFromFloatPixels(*ctx_, width, height, (float*)pixels);

    // 立方图转换需要颜色渲染通道（cubeColorPass_），CreateFromFile路径需自行创建
    createRenderPasses(*ctx_);

    // 创建渲染管线来将equirectangular映射转换为立方图
    createCubePipeline();

    // 创建环境立方图（作为颜色附件渲染 mip0，kPrefilterMips 级 mip 由 setupIBL blit 生成，供预滤波 lod 采样；
    // blit 生成 mip 链要求图像同时具备 TRANSFER_SRC（源）与 TRANSFER_DST（目标）usage）
    envCubemap_.Create(*ctx_, kEnvSize, kEnvSize, VK_FORMAT_R16G16B16A16_SFLOAT,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT, kPrefilterMips,
                       VK_SAMPLE_COUNT_1_BIT, 6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, VK_IMAGE_VIEW_TYPE_CUBE);

    // 创建帧缓冲区和渲染目标
    createCubeFramebuffer();

    // 渲染立方图的6个面
    renderCubeMapFaces(tempTexture);

    // 清理临时资源
    destroyCubeFramebuffer();
    destroyCubePipeline();
    tempTexture.Destroy();

    // 清理立方图转换期描述符资源（cubeColorPass_留给后续IBL使用，Destroy时统一释放）
    {
        const VkDevice dev = ctx_->Device();
        if (cubeDescriptorPool_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(dev, cubeDescriptorPool_, nullptr);
            cubeDescriptorPool_ = VK_NULL_HANDLE;
        }
        if (cubeSetLayout_ != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(dev, cubeSetLayout_, nullptr);
            cubeSetLayout_ = VK_NULL_HANDLE;
        }
        cubeSet_ = VK_NULL_HANDLE;
    }

    LOG_INFO("Successfully created cubemap from HDR");
    return true;
}

void EnvironmentLighting::renderCubeMapFaces(Texture& sourceTexture)
{
    const VkDevice device = ctx_->Device();

    // 创建描述符池 + 集，绑定临时等距柱状投影纹理（sourceTexture）
    {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = 1;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;
        VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &cubeDescriptorPool_), "创建立方图转换描述符池");

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = cubeDescriptorPool_;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &cubeSetLayout_;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &cubeSet_), "分配立方图转换描述符集");

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = sourceTexture.View();
        imageInfo.sampler = sourceTexture.Sampler();

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = cubeSet_;
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // 渲染每个面（全屏三角形 + push constant 面基矩阵）
    for (uint32_t face = 0; face < 6; ++face)
    {
        VkCommandBuffer commandBuffer = beginCommandBuffer();
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = cubeColorPass_;
        renderPassInfo.framebuffer = cubeFramebuffer_[face];
        renderPassInfo.renderArea = {{0, 0}, {kEnvSize, kEnvSize}};

        VkClearValue clearValue{};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        cubePipe_.Bind(commandBuffer);
        cubePipe_.BindDescriptorSets(commandBuffer, {cubeSet_});

        // 推送常量：面基矩阵（与程序化路径同约定）
        PushFace pc{};
        pc.basis[0] = glm::vec4(kCubeFaces[face].sVec, 0.0f);
        pc.basis[1] = glm::vec4(kCubeFaces[face].tVec, 0.0f);
        pc.basis[2] = glm::vec4(kCubeFaces[face].major, 0.0f);
        pc.roughness = 0.0f;
        vkCmdPushConstants(commandBuffer, cubePipe_.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushFace), &pc);

        VkViewport viewport{};
        viewport.width = static_cast<float>(kEnvSize);
        viewport.height = static_cast<float>(kEnvSize);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{{0, 0}, {kEnvSize, kEnvSize}};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdDraw(commandBuffer, 3, 1, 0, 0); // 全屏三角形

        vkCmdEndRenderPass(commandBuffer);
        endCommandBuffer(commandBuffer);
    }
}

void EnvironmentLighting::destroyCubePipeline()
{
    cubePipe_.Release();
}

void EnvironmentLighting::destroyCubeFramebuffer()
{
    const VkDevice device = ctx_->Device();

    for (uint32_t i = 0; i < 6; ++i)
    {
        if (cubeFramebuffer_[i] != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device, cubeFramebuffer_[i], nullptr);
            cubeFramebuffer_[i] = VK_NULL_HANDLE;
        }
        if (cubeFaceViews_[i] != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, cubeFaceViews_[i], nullptr);
            cubeFaceViews_[i] = VK_NULL_HANDLE;
        }
    }
}

void EnvironmentLighting::createCubePipeline()
{
    const VkDevice device = ctx_->Device();

    // 描述符集布局：binding 0 = 等距柱状投影纹理（combined image sampler，片段着色器采样）
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
        VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &cubeSetLayout_),
                 "创建立方图转换描述符布局");
    }

    // 加载预编译SPIR-V着色器（顶点复用env_conv.vert：全屏三角形+push constant面基矩阵）
    auto vert = Render::ShaderModuleHandle(device, Render::ReadShaderFile("shaders/env_conv.vert.spv"));
    auto frag = Render::ShaderModuleHandle(device, Render::ReadShaderFile("shaders/equirect_to_cube.frag.spv"));

    Render::GraphicsPipelineConfig config;
    config.setLayouts = {cubeSetLayout_};
    config.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushFace)}};
    config.depthTest = false;
    config.depthWrite = false;
    config.cullMode = VK_CULL_MODE_NONE;

    cubePipe_ = Render::GraphicsPipeline(device, cubeColorPass_, std::move(vert), std::move(frag), config);
}

void EnvironmentLighting::createCubeFramebuffer()
{
    const VkDevice device = ctx_->Device();

    // 为每个面创建独立的 2D 视图（baseArrayLayer=面索引, layerCount=1），
    // 再以该视图为颜色附件建帧缓冲。cubeColorPass_ 已由 createRenderPasses 创建。
    for (uint32_t i = 0; i < 6; ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = envCubemap_.Get();
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = i;
        viewInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &cubeFaceViews_[i]), "创建立方图面视图");

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = cubeColorPass_;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &cubeFaceViews_[i];
        framebufferInfo.width = kEnvSize;
        framebufferInfo.height = kEnvSize;
        framebufferInfo.layers = 1;

        VK_CHECK(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &cubeFramebuffer_[i]), "创建立方图帧缓冲区");
    }
}

void EnvironmentLighting::createSampler(const Context& ctx)
{
    const VkDevice device = ctx.Device();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    // maxLod 必须 <= 被采样图像 levelCount - 1（VUID-VkSamplerCreateInfo-maxLod-01973）。
    // envCubemap_ 共 IblMipLevels() 级 mip，故上限为 IblMipLevels()-1；写成 IblMipLevels()
    // 会越界 1 级，AMD 780M 驱动据此做未定义的 lod 钳制/映射，可致 GPU 页错误 → DEVICE_LOST。
    // prefilter.frag 实际请求的最大 lod 为 roughness*4.0 = 4.0，与本上限一致。
    samplerInfo.maxLod = static_cast<float>(IblMipLevels() - 1);

    VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &sampler_), "创建采样器");
}

// Helper function to begin command buffer recording
VkCommandBuffer EnvironmentLighting::beginCommandBuffer()
{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = ctx_->CommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(ctx_->Device(), &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

// Helper function to end command buffer recording and submit
void EnvironmentLighting::endCommandBuffer(VkCommandBuffer commandBuffer)
{
    VK_CHECK(vkEndCommandBuffer(commandBuffer), "结束IBL一次性命令缓冲");

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkFence fence = VK_NULL_HANDLE;
    // 提交/等待结果必须检查：设备丢失若在此处被静默吞掉，会推迟到下一次受检提交，
    // 把故障定位误导到无关阶段
    const VkResult submitRes = vkQueueSubmit(ctx_->GraphicsQueue(), 1, &submitInfo, fence);
    if (submitRes != VK_SUCCESS)
        LOG_ERROR("vkQueueSubmit 失败 VkResult=" << int(submitRes));
    VK_CHECK(vkQueueWaitIdle(ctx_->GraphicsQueue()), "等待IBL一次性命令完成");

    vkFreeCommandBuffers(ctx_->Device(), ctx_->CommandPool(), 1, &commandBuffer);
}

} // namespace BigHero
