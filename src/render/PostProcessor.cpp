#include "render/PostProcessor.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Context.h"

#include <array>

namespace BigHero::Render
{
void PostProcessor::Init(const Context& ctx, VkExtent2D extent, VkFormat colorFormat, VkSampleCountFlagBits samples,
                         const std::vector<VkImageView>& swapchainViews)
{
    device_ = ctx.Device();
    extent_ = extent;
    colorFormat_ = colorFormat;
    samples_ = samples;
    halfExtent_ = {std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)};

    CreateImages(ctx);
    CreateRenderPasses();
    CreateFramebuffers(swapchainViews);
    CreateDescriptorResources(ctx);
    CreatePipelines(ctx);
    UpdateDescriptorSets();

    initialized_ = true;
    LOG_INFO("后处理初始化完成: " << extent.width << "x" << extent.height << " (半分辨率 " << halfExtent_.width << "x"
                                  << halfExtent_.height << ")");
}

void PostProcessor::Destroy()
{
    if (!device_)
        return;
    vkDeviceWaitIdle(device_);

    DestroyPipelines();
    DestroyFramebuffers();

    if (descPool_ != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device_, descPool_, nullptr);
    if (descSetLayout_ != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device_, descSetLayout_, nullptr);
    if (sampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_, sampler_, nullptr);
    if (postRenderPass_ != VK_NULL_HANDLE)
        vkDestroyRenderPass(device_, postRenderPass_, nullptr);
    if (linearizeRenderPass_ != VK_NULL_HANDLE)
        vkDestroyRenderPass(device_, linearizeRenderPass_, nullptr);
    if (outputRenderPass_ != VK_NULL_HANDLE)
        vkDestroyRenderPass(device_, outputRenderPass_, nullptr);

    offscreenMsaaColor_.Destroy();
    offscreenResolve_.Destroy();
    brightImage_.Destroy();
    blurImageA_.Destroy();
    blurImageB_.Destroy();
    linearDepthImage_.Destroy();
    dofImage_.Destroy();

    initialized_ = false;
}

void PostProcessor::Recreate(const Context& ctx, VkExtent2D extent, const std::vector<VkImageView>& swapchainViews)
{
    if (!initialized_)
        return;
    vkDeviceWaitIdle(device_);
    extent_ = extent;
    halfExtent_ = {std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)};

    DestroyFramebuffers();
    DestroyPipelines();
    offscreenMsaaColor_.Destroy();
    offscreenResolve_.Destroy();
    brightImage_.Destroy();
    blurImageA_.Destroy();
    blurImageB_.Destroy();
    linearDepthImage_.Destroy();
    dofImage_.Destroy();

    CreateImages(ctx);
    CreateFramebuffers(swapchainViews);
    CreatePipelines(ctx);
    UpdateDescriptorSets();
    LOG_INFO("后处理资源已重建: " << extent.width << "x" << extent.height);
}

void PostProcessor::CreateImages(const Context& ctx)
{
    const bool useMsaa = samples_ != VK_SAMPLE_COUNT_1_BIT;

    if (useMsaa)
    {
        offscreenMsaaColor_.Create(ctx, extent_.width, extent_.height, colorFormat_,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                   VK_IMAGE_ASPECT_COLOR_BIT, 1, samples_);
    }

    offscreenResolve_.Create(ctx, extent_.width, extent_.height, colorFormat_,
                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    const VkImageUsageFlags postUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    brightImage_.Create(ctx, halfExtent_.width, halfExtent_.height, colorFormat_, postUsage,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    blurImageA_.Create(ctx, halfExtent_.width, halfExtent_.height, colorFormat_, postUsage,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    blurImageB_.Create(ctx, halfExtent_.width, halfExtent_.height, colorFormat_, postUsage,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

    // 升级 22：景深用中间图（仅 MSAA 路径）
    if (useMsaa)
    {
        // 线性深度图：R32F，作颜色附件被写入、被景深着色器采样
        linearDepthImage_.Create(ctx, extent_.width, extent_.height, VK_FORMAT_R32_SFLOAT,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        // 景深输出图：与原离屏同格式，bloom 链改从此图读取
        dofImage_.Create(ctx, extent_.width, extent_.height, colorFormat_,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    }
}

void PostProcessor::CreateRenderPasses()
{
    // 后处理渲染通道：单颜色附件，最终布局 SHADER_READ_ONLY
    {
        VkAttachmentDescription att{};
        att.format = colorFormat_;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;

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
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dep;
        VK_CHECK(vkCreateRenderPass(device_, &info, nullptr, &postRenderPass_), "创建后处理渲染通道");
    }

    // 升级 22：深度线性化专用渲染通道（R32F 单颜色附件，匹配线性深度图格式）
    {
        VkAttachmentDescription att{};
        att.format = VK_FORMAT_R32_SFLOAT;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;

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
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dep;
        VK_CHECK(vkCreateRenderPass(device_, &info, nullptr, &linearizeRenderPass_), "创建深度线性化渲染通道");
    }

    // 输出渲染通道：写入交换链，最终布局 COLOR_ATTACHMENT_OPTIMAL
    {
        VkAttachmentDescription att{};
        att.format = colorFormat_;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;

        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &att;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        VK_CHECK(vkCreateRenderPass(device_, &info, nullptr, &outputRenderPass_), "创建输出渲染通道");
    }
}

void PostProcessor::CreateFramebuffers(const std::vector<VkImageView>& swapchainViews)
{
    auto createPostFb = [&](VkImageView view) -> VkFramebuffer
    {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = postRenderPass_;
        info.attachmentCount = 1;
        info.pAttachments = &view;
        info.width = halfExtent_.width;
        info.height = halfExtent_.height;
        info.layers = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        VK_CHECK(vkCreateFramebuffer(device_, &info, nullptr, &fb), "创建后处理帧缓冲");
        return fb;
    };
    brightFramebuffer_ = createPostFb(brightImage_.View());
    blurAFramebuffer_ = createPostFb(blurImageA_.View());
    blurBFramebuffer_ = createPostFb(blurImageB_.View());

    // 升级 22：景深全分辨率帧缓冲（线性深度图 + 景深输出图）
    if (UseMsaa())
    {
        auto createFullFb = [&](VkImageView view, VkRenderPass rp) -> VkFramebuffer
        {
            VkFramebufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = rp;
            info.attachmentCount = 1;
            info.pAttachments = &view;
            info.width = extent_.width;
            info.height = extent_.height;
            info.layers = 1;
            VkFramebuffer fb = VK_NULL_HANDLE;
            VK_CHECK(vkCreateFramebuffer(device_, &info, nullptr, &fb), "创建景深帧缓冲");
            return fb;
        };
        depthLinearizeFramebuffer_ = createFullFb(linearDepthImage_.View(), linearizeRenderPass_);
        dofFramebuffer_ = createFullFb(dofImage_.View(), postRenderPass_);
    }

    outputFramebuffers_.reserve(swapchainViews.size());
    for (VkImageView view : swapchainViews)
    {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = outputRenderPass_;
        info.attachmentCount = 1;
        info.pAttachments = &view;
        info.width = extent_.width;
        info.height = extent_.height;
        info.layers = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        VK_CHECK(vkCreateFramebuffer(device_, &info, nullptr, &fb), "创建输出帧缓冲");
        outputFramebuffers_.push_back(fb);
    }
}

void PostProcessor::CreateDescriptorResources(const Context& ctx)
{
    (void)ctx;
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descSetLayout_), "创建后处理描述符布局");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 16;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 8;
    VK_CHECK(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descPool_), "创建后处理描述符池");

    std::array<VkDescriptorSetLayout, 8> layouts = {descSetLayout_, descSetLayout_, descSetLayout_, descSetLayout_,
                                                    descSetLayout_, descSetLayout_, descSetLayout_, descSetLayout_};
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descPool_;
    allocInfo.descriptorSetCount = 8;
    allocInfo.pSetLayouts = layouts.data();
    std::array<VkDescriptorSet, 8> sets{};
    VK_CHECK(vkAllocateDescriptorSets(device_, &allocInfo, sets.data()), "分配后处理描述符集");
    brightDescSet_ = sets[0];
    blurHDescSet_ = sets[1];
    blurVDescSet_ = sets[2];
    compositeDescSet_ = sets[3];
    depthLinearizeDescSet_ = sets[4];
    dofDescSet_ = sets[5];

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    VK_CHECK(vkCreateSampler(device_, &samplerInfo, nullptr, &sampler_), "创建后处理采样器");
}

void PostProcessor::CreatePipelines(const Context& ctx)
{
    GraphicsPipelineConfig cfg;
    cfg.vertexBindings = {};
    cfg.vertexAttributes = {};
    cfg.cullMode = VK_CULL_MODE_NONE;
    cfg.depthTest = false;
    cfg.depthWrite = false;
    cfg.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
    cfg.colorAttachmentCount = 1;
    cfg.setLayouts = {descSetLayout_};

    const auto fullscreenSpv = ReadShaderFile("shaders/pp_fullscreen.vert.spv");

    // 亮部提取管线
    {
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_bright.frag.spv"));
        brightPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), postRenderPass_, std::move(v), std::move(f), cfg);
    }

    // 模糊管线
    {
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_blur.frag.spv"));
        blurPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), postRenderPass_, std::move(v), std::move(f), cfg);
    }

    // 合成管线（push constant 含 Bloom + 色调分级参数，共 7 个 float = 28 字节）
    {
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 28}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_composite.frag.spv"));
        compositePipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), outputRenderPass_, std::move(v), std::move(f), cfg);
    }

    // 升级 22：深度线性化管线（MSAA 深度 → 线性深度 R32F）
    if (UseMsaa())
    {
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_depth_linearize.frag.spv"));
        depthLinearizePipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), linearizeRenderPass_, std::move(v), std::move(f), cfg);
    }
    // 升级 22：景深（DoF）管线（场景颜色 + 线性深度 → 虚化输出）
    if (UseMsaa())
    {
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_dof.frag.spv"));
        dofPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), postRenderPass_, std::move(v), std::move(f), cfg);
    }
}

void PostProcessor::UpdateDescriptorSets()
{
    auto writeImage = [&](VkDescriptorSet set, uint32_t binding, VkImageView view)
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = view;
        imageInfo.sampler = sampler_;
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.dstBinding = binding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    };

    // 升级 22：MSAA 路径下 bloom 链改从景深输出图读取；非 MSAA 沿用离屏解析图
    const VkImageView sceneColorView = UseMsaa() ? dofImage_.View() : offscreenResolve_.View();
    writeImage(brightDescSet_, 0, sceneColorView);
    writeImage(blurHDescSet_, 0, brightImage_.View());
    writeImage(blurVDescSet_, 0, blurImageA_.View());
    writeImage(compositeDescSet_, 0, sceneColorView);
    writeImage(compositeDescSet_, 1, blurImageB_.View());
    // 升级 22：深度线性化（场景 MSAA 深度）与景深（场景颜色 + 线性深度）
    if (UseMsaa())
    {
        // 深度图需以 DEPTH_STENCIL_READ_ONLY_OPTIMAL 布局采样（不能用通用 SHADER_READ_ONLY）
        VkDescriptorImageInfo depthInfo{};
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthInfo.imageView = sceneDepthView_;
        depthInfo.sampler = sampler_;
        VkWriteDescriptorSet depthWrite{};
        depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        depthWrite.dstSet = depthLinearizeDescSet_;
        depthWrite.dstBinding = 0;
        depthWrite.descriptorCount = 1;
        depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        depthWrite.pImageInfo = &depthInfo;
        vkUpdateDescriptorSets(device_, 1, &depthWrite, 0, nullptr);

        writeImage(dofDescSet_, 0, offscreenResolve_.View());
        writeImage(dofDescSet_, 1, linearDepthImage_.View());
    }
}

void PostProcessor::DestroyFramebuffers()
{
    if (brightFramebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, brightFramebuffer_, nullptr);
    if (blurAFramebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, blurAFramebuffer_, nullptr);
    if (blurBFramebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, blurBFramebuffer_, nullptr);
    if (depthLinearizeFramebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, depthLinearizeFramebuffer_, nullptr);
    if (dofFramebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, dofFramebuffer_, nullptr);
    depthLinearizeFramebuffer_ = VK_NULL_HANDLE;
    dofFramebuffer_ = VK_NULL_HANDLE;
    for (VkFramebuffer fb : outputFramebuffers_)
        vkDestroyFramebuffer(device_, fb, nullptr);
    outputFramebuffers_.clear();
    brightFramebuffer_ = VK_NULL_HANDLE;
    blurAFramebuffer_ = VK_NULL_HANDLE;
    blurBFramebuffer_ = VK_NULL_HANDLE;
}

void PostProcessor::DestroyPipelines()
{
    brightPipeline_.reset();
    blurPipeline_.reset();
    compositePipeline_.reset();
    depthLinearizePipeline_.reset();
    dofPipeline_.reset();
}

void PostProcessor::RecordBloom(VkCommandBuffer cmd, uint32_t swapchainIndex, VkExtent2D extent, float camNear,
                                float camFar)
{
    if (!initialized_)
        return;

    std::array<VkClearValue, 1> clear{};
    clear[0].color.float32[0] = 0.0f;
    clear[0].color.float32[1] = 0.0f;
    clear[0].color.float32[2] = 0.0f;
    clear[0].color.float32[3] = 1.0f;

    auto beginPass = [&](VkRenderPass rp, VkFramebuffer fb, VkExtent2D ext)
    {
        VkRenderPassBeginInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = rp;
        info.framebuffer = fb;
        info.renderArea.offset = {0, 0};
        info.renderArea.extent = ext;
        info.clearValueCount = 1;
        info.pClearValues = clear.data();
        vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
    };

    // ---- 升级 22：景深（DoF）链（仅 MSAA 路径）----
    if (UseMsaa() && sceneDepthImage_ != VK_NULL_HANDLE)
    {
        // 1) 深度布局转换：场景通道结束后深度仍在 DEPTH_STENCIL_ATTACHMENT_OPTIMAL，
        //    采样前转为只读，供景深 Pass 读取 MSAA 深度。
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = sceneDepthImage_;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            b.subresourceRange.baseMipLevel = 0;
            b.subresourceRange.levelCount = 1;
            b.subresourceRange.baseArrayLayer = 0;
            b.subresourceRange.layerCount = 1;
            b.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }

        // 2) 深度线性化：MSAA 深度 → 线性深度（R32F）
        beginPass(linearizeRenderPass_, depthLinearizeFramebuffer_, extent_);
        depthLinearizePipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depthLinearizePipeline_->pipelineLayout, 0, 1,
                                &depthLinearizeDescSet_, 0, nullptr);
        struct LinearizeParams
        {
            float nearPlane;
            float farPlane;
            float pad0;
            float pad1;
        } lp{camNear, camFar, 0.0f, 0.0f};
        vkCmdPushConstants(cmd, depthLinearizePipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(lp),
                           &lp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        // 3) 景深虚化：场景颜色 + 线性深度 → dofImage_
        beginPass(postRenderPass_, dofFramebuffer_, extent_);
        dofPipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dofPipeline_->pipelineLayout, 0, 1, &dofDescSet_,
                                0, nullptr);
        struct DofParams
        {
            float focusDistance;
            float aperture;
            float maxBlur;
            float enabled;
        } dp{dofFocusDistance, dofAperture, dofMaxBlur, dofEnabled ? 1.0f : 0.0f};
        vkCmdPushConstants(cmd, dofPipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dp), &dp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    // Pass 1: 亮部提取
    beginPass(postRenderPass_, brightFramebuffer_, halfExtent_);
    brightPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, brightPipeline_->pipelineLayout, 0, 1,
                            &brightDescSet_, 0, nullptr);
    struct BrightParams
    {
        float threshold;
        float softKnee;
    } params{bloomThreshold, bloomSoftKnee};
    vkCmdPushConstants(cmd, brightPipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(params), &params);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // Pass 2: 水平模糊
    beginPass(postRenderPass_, blurAFramebuffer_, halfExtent_);
    blurPipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline_->pipelineLayout, 0, 1, &blurHDescSet_,
                            0, nullptr);
    struct BlurParams
    {
        float dx;
        float dy;
    } blurH{1.0f / halfExtent_.width, 0.0f};
    vkCmdPushConstants(cmd, blurPipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(blurH), &blurH);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // Pass 3: 垂直模糊
    beginPass(postRenderPass_, blurBFramebuffer_, halfExtent_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline_->pipelineLayout, 0, 1, &blurVDescSet_,
                            0, nullptr);
    BlurParams blurV{0.0f, 1.0f / halfExtent_.height};
    vkCmdPushConstants(cmd, blurPipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(blurV), &blurV);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // Pass 4: 合成到交换链
    beginPass(outputRenderPass_, outputFramebuffers_[swapchainIndex], extent);
    compositePipeline_->Bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline_->pipelineLayout, 0, 1,
                            &compositeDescSet_, 0, nullptr);
    struct CompositeParams
    {
        float bloomStrength;
        float exposure;
        float saturation;
        float contrast;
        float lift;
        float gain;
        float gamma;
    } comp{bloomStrength, exposure, gradeSaturation, gradeContrast, gradeLift, gradeGain, gradeGamma};
    vkCmdPushConstants(cmd, compositePipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(comp), &comp);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}
} // namespace BigHero::Render
