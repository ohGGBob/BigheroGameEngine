#include "render/PostProcessor.h"
#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Context.h"
#include "render/ubo_structs.h"

#include <array>
#include <cmath>

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
    InitAdaptImages(ctx);
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
    mbImage_.Destroy(); // 升级 23
    lum64Image_.Destroy();  // 升级 26：自动曝光亮度链
    lum8Image_.Destroy();
    lum1Image_.Destroy();
    adaptImageA_.Destroy();
    adaptImageB_.Destroy();
    taaImageA_.Destroy(); // 升级 28：TAA 历史 ping-pong
    taaImageB_.Destroy();

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
    mbImage_.Destroy(); // 升级 23
    lum64Image_.Destroy();  // 升级 26：自动曝光亮度链
    lum8Image_.Destroy();
    lum1Image_.Destroy();
    adaptImageA_.Destroy();
    adaptImageB_.Destroy();
    taaImageA_.Destroy(); // 升级 28：TAA 历史 ping-pong（尺寸变化历史失效）
    taaImageB_.Destroy();

    CreateImages(ctx);
    InitAdaptImages(ctx);
    CreateFramebuffers(swapchainViews);
    CreatePipelines(ctx);
    UpdateDescriptorSets();
    ResetTaa(); // 升级 28：重建后历史失效，下一帧直通重建
    taaIndex_ = 0;
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
        // 升级 23：运动模糊输出图：与景深同格式，bloom 链改从此图读取
        mbImage_.Create(ctx, extent_.width, extent_.height, colorFormat_,
                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // 升级 26：自动曝光亮度链小图（固定尺寸，与交换链/MSAA 无关；复用 postRenderPass_ 格式）
    lum64Image_.Create(ctx, 64, 64, colorFormat_, postUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       VK_IMAGE_ASPECT_COLOR_BIT);
    lum8Image_.Create(ctx, 8, 8, colorFormat_, postUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT);
    lum1Image_.Create(ctx, 1, 1, colorFormat_, postUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      VK_IMAGE_ASPECT_COLOR_BIT);
    adaptImageA_.Create(ctx, 1, 1, colorFormat_, postUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT);
    adaptImageB_.Create(ctx, 1, 1, colorFormat_, postUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        VK_IMAGE_ASPECT_COLOR_BIT);

    // 升级 28：TAA 历史 ping-pong（全分辨率，与交换链同格式）
    taaImageA_.Create(ctx, extent_.width, extent_.height, colorFormat_, postUsage,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    taaImageB_.Create(ctx, extent_.width, extent_.height, colorFormat_, postUsage,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    // 初始转为 SHADER_READ_ONLY：首帧历史未采样（newFrame 直通）时描述符布局也合法
    taaImageA_.TransitionLayout(ctx, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    taaImageB_.TransitionLayout(ctx, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

    // 升级 26：自动曝光亮度链帧缓冲（固定小尺寸，单附件 + postRenderPass_）
    {
        auto createSizeFb = [&](VkImageView view, uint32_t w, uint32_t h) -> VkFramebuffer
        {
            VkFramebufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = postRenderPass_;
            info.attachmentCount = 1;
            info.pAttachments = &view;
            info.width = w;
            info.height = h;
            info.layers = 1;
            VkFramebuffer fb = VK_NULL_HANDLE;
            VK_CHECK(vkCreateFramebuffer(device_, &info, nullptr, &fb), "创建自动曝光帧缓冲");
            return fb;
        };
        lum64Framebuffer_ = createSizeFb(lum64Image_.View(), 64, 64);
        lum8Framebuffer_ = createSizeFb(lum8Image_.View(), 8, 8);
        lum1Framebuffer_ = createSizeFb(lum1Image_.View(), 1, 1);
        adaptAFramebuffer_ = createSizeFb(adaptImageA_.View(), 1, 1);
        adaptBFramebuffer_ = createSizeFb(adaptImageB_.View(), 1, 1);
        // 升级 28：TAA 历史 ping-pong 帧缓冲（全分辨率）
        taaAFramebuffer_ = createSizeFb(taaImageA_.View(), extent_.width, extent_.height);
        taaBFramebuffer_ = createSizeFb(taaImageB_.View(), extent_.width, extent_.height);
    }

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
        mbFramebuffer_ = createFullFb(mbImage_.View(), postRenderPass_);
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
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // 升级 25：合成 Pass 采样线性深度图（体积雾光线终点），亮部/模糊等其余 Pass 不用 b2
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // 升级 26：合成 Pass 采样 1x1 适应亮度图（自动曝光），其余 Pass 不用 b3
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // 升级 27：合成 Pass 读取场景级联 UBO（雾阴影采样用级联矩阵/分割/偏移）
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // 升级 27：合成 Pass 采样 CSM 深度图集（雾光线步进点遮挡判定）
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    VK_CHECK(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descSetLayout_), "创建后处理描述符布局");

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 40;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // 升级 27：合成 Pass 级联 UBO
    poolSizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 16;
    VK_CHECK(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descPool_), "创建后处理描述符池");

    std::array<VkDescriptorSetLayout, 15> layouts = {descSetLayout_, descSetLayout_, descSetLayout_, descSetLayout_,
                                                     descSetLayout_, descSetLayout_, descSetLayout_, descSetLayout_,
                                                     descSetLayout_, descSetLayout_, descSetLayout_, descSetLayout_,
                                                     descSetLayout_, descSetLayout_, descSetLayout_};
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descPool_;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();
    std::array<VkDescriptorSet, 15> sets{};
    VK_CHECK(vkAllocateDescriptorSets(device_, &allocInfo, sets.data()), "分配后处理描述符集");
    brightDescSet_ = sets[0];
    blurHDescSet_ = sets[1];
    blurVDescSet_ = sets[2];
    compositeDescSet_ = sets[3];
    depthLinearizeDescSet_ = sets[4];
    dofDescSet_ = sets[5];
    mbDescSet_ = sets[6];   // 升级 23：运动模糊描述符集
    lumStartDescSet_ = sets[7];  // 升级 26：亮度链描述符集
    lumDownDescA_ = sets[8];
    lumDownDescB_ = sets[9];
    adaptDescA_ = sets[10];
    adaptDescB_ = sets[11];
    taaDescSetA_ = sets[12]; // 升级 28：TAA 描述符集
    taaDescSetB_ = sets[13];

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

    // 合成管线（push constant = Bloom/色调 + 体积雾 + 相机环境，共 128 字节，见 pp_composite.frag.glsl）
    {
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128}};
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
    // 升级 23：相机运动模糊（Motion Blur）管线（DoF 输出 + MSAA 深度 → 带拖尾输出）
    if (UseMsaa())
    {
        // push constant = mat4 重投影(64B) + 4 float(16B) = 80 字节，低于 128 上限
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_motion_blur.frag.spv"));
        mbPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), postRenderPass_, std::move(v), std::move(f), cfg);
    }

    // 升级 26：自动曝光亮度链管线（复用 postRenderPass_，push constant 16 字节内）
    {
        cfg.pushConstants = {}; // 起始 Pass 无参数（目标尺寸为编译期常量）
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_lum_start.frag.spv"));
        lumStartPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), postRenderPass_, std::move(v), std::move(f), cfg);
    }
    {
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16}}; // 源图边长（float）+ 3 pad
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_lum_down.frag.spv"));
        lumDownPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), postRenderPass_, std::move(v), std::move(f), cfg);
    }
    {
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16}}; // factor/reset（2 float）+ 2 pad
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_adapt.frag.spv"));
        adaptPipeline_ =
            std::make_unique<GraphicsPipeline>(ctx.Device(), postRenderPass_, std::move(v), std::move(f), cfg);
    }
    {
        // 升级 28：TAA 管线（push constant = mat4 重投影 64B + 6 float 24B = 88B）
        cfg.pushConstants = {{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 88}};
        ShaderModuleHandle v(ctx.Device(), fullscreenSpv);
        ShaderModuleHandle f(ctx.Device(), ReadShaderFile("shaders/pp_taa.frag.spv"));
        taaPipeline_ =
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

    // 升级 23：MSAA 路径下 bloom 链改从运动模糊输出图读取；非 MSAA 沿用离屏解析图
    // 运动模糊 Pass 始终运行（关闭时着色器直通），故 bloom 稳定读 mbImage_
    const VkImageView sceneColorView = UseMsaa() ? mbImage_.View() : offscreenResolve_.View();
    writeImage(brightDescSet_, 0, sceneColorView);
    writeImage(blurHDescSet_, 0, brightImage_.View());
    writeImage(blurVDescSet_, 0, blurImageA_.View());
    writeImage(compositeDescSet_, 0, sceneColorView);
    writeImage(compositeDescSet_, 1, blurImageB_.View());
    // 升级 25：合成 Pass b2 = 线性深度图（体积雾光线终点）；非 MSAA 无线性深度图，绑离屏解析图占位
    // （雾关闭时着色器在采样前早退，占位图不参与渲染）
    writeImage(compositeDescSet_, 2, UseMsaa() ? linearDepthImage_.View() : offscreenResolve_.View());
    // 升级 22/23：深度线性化（场景 MSAA 深度）、景深（场景颜色 + 线性深度）、运动模糊（DoF 输出 + 深度）
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

        // 升级 23：运动模糊（b0=DoF 输出场景颜色，b1=MSAA 深度用于重建 NDC）
        VkDescriptorImageInfo mbDepthInfo{};
        mbDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        mbDepthInfo.imageView = sceneDepthView_;
        mbDepthInfo.sampler = sampler_;
        VkWriteDescriptorSet mbDepthWrite{};
        mbDepthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        mbDepthWrite.dstSet = mbDescSet_;
        mbDepthWrite.dstBinding = 1;
        mbDepthWrite.descriptorCount = 1;
        mbDepthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        mbDepthWrite.pImageInfo = &mbDepthInfo;
        vkUpdateDescriptorSets(device_, 1, &mbDepthWrite, 0, nullptr);
        writeImage(mbDescSet_, 0, dofImage_.View());

        // 升级 28：TAA（b0=当前帧场景色[DoF+MB 输出]，b1=对侧历史图，b2=MSAA 深度用于重投影）
        writeImage(taaDescSetA_, 0, mbImage_.View());
        writeImage(taaDescSetA_, 1, taaImageB_.View());
        writeImage(taaDescSetB_, 0, mbImage_.View());
        writeImage(taaDescSetB_, 1, taaImageA_.View());
        VkDescriptorImageInfo taaDepthInfo{};
        taaDepthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        taaDepthInfo.imageView = sceneDepthView_;
        taaDepthInfo.sampler = sampler_;
        VkWriteDescriptorSet taaDepthWrites[2]{};
        taaDepthWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        taaDepthWrites[0].dstSet = taaDescSetA_;
        taaDepthWrites[0].dstBinding = 2;
        taaDepthWrites[0].descriptorCount = 1;
        taaDepthWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        taaDepthWrites[0].pImageInfo = &taaDepthInfo;
        taaDepthWrites[1] = taaDepthWrites[0];
        taaDepthWrites[1].dstSet = taaDescSetB_;
        vkUpdateDescriptorSets(device_, 2, taaDepthWrites, 0, nullptr);
    }

    // 升级 26：自动曝光亮度链（两路径共用；源图固定为本帧离屏解析图）
    writeImage(lumStartDescSet_, 0, offscreenResolve_.View());
    writeImage(lumDownDescA_, 0, lum64Image_.View());
    writeImage(lumDownDescB_, 0, lum8Image_.View());
    // 适应 ping-pong：偶帧写 A 读 B，奇帧写 B 读 A（RecordBloom 内按 adaptIndex_ 选用）
    writeImage(adaptDescA_, 0, lum1Image_.View());
    writeImage(adaptDescA_, 1, adaptImageB_.View());
    writeImage(adaptDescB_, 0, lum1Image_.View());
    writeImage(adaptDescB_, 1, adaptImageA_.View());
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
    if (mbFramebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, mbFramebuffer_, nullptr);
    if (lum64Framebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, lum64Framebuffer_, nullptr);
    if (lum8Framebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, lum8Framebuffer_, nullptr);
    if (lum1Framebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, lum1Framebuffer_, nullptr);
    if (adaptAFramebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, adaptAFramebuffer_, nullptr);
    if (adaptBFramebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, adaptBFramebuffer_, nullptr);
    if (taaAFramebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, taaAFramebuffer_, nullptr);
    if (taaBFramebuffer_ != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device_, taaBFramebuffer_, nullptr);
    depthLinearizeFramebuffer_ = VK_NULL_HANDLE;
    dofFramebuffer_ = VK_NULL_HANDLE;
    mbFramebuffer_ = VK_NULL_HANDLE;
    lum64Framebuffer_ = VK_NULL_HANDLE;
    lum8Framebuffer_ = VK_NULL_HANDLE;
    lum1Framebuffer_ = VK_NULL_HANDLE;
    adaptAFramebuffer_ = VK_NULL_HANDLE;
    adaptBFramebuffer_ = VK_NULL_HANDLE;
    taaAFramebuffer_ = VK_NULL_HANDLE;
    taaBFramebuffer_ = VK_NULL_HANDLE;
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
    mbPipeline_.reset(); // 升级 23
    lumStartPipeline_.reset(); // 升级 26：自动曝光亮度链
    lumDownPipeline_.reset();
    adaptPipeline_.reset();
    taaPipeline_.reset(); // 升级 28：TAA
}

// 升级 26：自适应亮度 ping-pong 图一次性初始化。
// 两张 1x1 图清为 log(0.18)（18% 灰基准的对数亮度）并转为 SHADER_READ_ONLY，
// 避免首帧适应 Pass 采样未定义内容；此后每帧由适应 Pass 交替写入。
void PostProcessor::InitAdaptImages(const Context& ctx)
{
    adaptImageA_.TransitionLayout(ctx, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    adaptImageB_.TransitionLayout(ctx, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    ctx.SubmitOneTime(
        [&](VkCommandBuffer cmd)
        {
            VkClearColorValue cv{};
            cv.float32[0] = -1.7146f; // log(0.18)
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(cmd, adaptImageA_.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &range);
            vkCmdClearColorImage(cmd, adaptImageB_.Get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &range);
        });
    adaptImageA_.TransitionLayout(ctx, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    adaptImageB_.TransitionLayout(ctx, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

    // ---- 升级 26：自动曝光亮度链（每帧常跑，开销可忽略；开关仅作用于合成端采样）----
    // 源图固定为本帧离屏解析图（MSAA 路径上 mbImage_ 此刻还是上一帧内容）
    {
        const VkExtent2D k64{64, 64};
        const VkExtent2D k8{8, 8};
        const VkExtent2D k1{1, 1};

        // 1) 场景 → 64x64 对数亮度
        beginPass(postRenderPass_, lum64Framebuffer_, k64);
        lumStartPipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lumStartPipeline_->pipelineLayout, 0, 1,
                                &lumStartDescSet_, 0, nullptr);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        struct LumDownParams
        {
            float srcSize;
            float pad0;
            float pad1;
            float pad2;
        };

        // 2) 64 → 8
        beginPass(postRenderPass_, lum8Framebuffer_, k8);
        lumDownPipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lumDownPipeline_->pipelineLayout, 0, 1,
                                &lumDownDescA_, 0, nullptr);
        const LumDownParams down64{64.0f, 0.0f, 0.0f, 0.0f};
        vkCmdPushConstants(cmd, lumDownPipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(down64),
                           &down64);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        // 3) 8 → 1（当前帧平均对数亮度）
        beginPass(postRenderPass_, lum1Framebuffer_, k1);
        lumDownPipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lumDownPipeline_->pipelineLayout, 0, 1,
                                &lumDownDescB_, 0, nullptr);
        const LumDownParams down8{8.0f, 0.0f, 0.0f, 0.0f};
        vkCmdPushConstants(cmd, lumDownPipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(down8),
                           &down8);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);

        // 4) 亮度适应 ping-pong：偶帧写 A 读 B，奇帧写 B 读 A
        struct AdaptParams
        {
            float factor;
            float reset;
            float pad0;
            float pad1;
        };
        const float factor = 1.0f - std::exp(-frameDelta_ * adaptationSpeed);
        const float reset = (frameCounter_ == 0) ? 1.0f : 0.0f;
        const VkFramebuffer adaptTarget = (adaptIndex_ & 1u) ? adaptBFramebuffer_ : adaptAFramebuffer_;
        const VkDescriptorSet adaptSrc = (adaptIndex_ & 1u) ? adaptDescB_ : adaptDescA_;
        beginPass(postRenderPass_, adaptTarget, k1);
        adaptPipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, adaptPipeline_->pipelineLayout, 0, 1, &adaptSrc,
                                0, nullptr);
        const AdaptParams ap{factor, reset, 0.0f, 0.0f};
        vkCmdPushConstants(cmd, adaptPipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ap), &ap);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    // ---- 升级 22：景深（DoF）链（仅 MSAA 路径）----
    // 深度布局已由渲染图在场景通道结束后转换为 DEPTH_STENCIL_READ_ONLY（post pass 输入），
    // 此处不再手动插入 barrier。
    if (UseMsaa() && sceneDepthImage_ != VK_NULL_HANDLE)
    {
        // 1) 深度线性化：MSAA 深度 → 线性深度（R32F）
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

    // 升级 23：相机运动模糊（Motion Blur）Pass（仅 MSAA 路径，紧随景深之后）
    // 关闭时着色器直通 DoF 输出，故始终运行以产出稳定 mbImage_ 供 bloom 读取
    if (UseMsaa() && sceneDepthImage_ != VK_NULL_HANDLE)
    {
        beginPass(postRenderPass_, mbFramebuffer_, extent_);
        mbPipeline_->Bind(cmd);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mbPipeline_->pipelineLayout, 0, 1, &mbDescSet_, 0,
                                nullptr);
        struct MbParams
        {
            glm::mat4 reproj; // prevVP × inverse(currVP)
            float strength;   // 拖尾强度
            float maxBlur;    // 速度向量长度上限
            float enabled;    // 1=启用，0=直通
            float maxSamples; // 采样数
        } mp{mbReproj_, mbStrength, mbMaxBlur, mbEnabled ? 1.0f : 0.0f, mbMaxSamples};
        static_assert(sizeof(MbParams) == 80, "MbParams 必须为 80 字节（mat4 + 4 float，push constant 上限内）");
        vkCmdPushConstants(cmd, mbPipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(mp), &mp);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
    }

    // ---- 升级 28：TAA 时间抗锯齿 Pass（仅 MSAA 路径，启用时输出 ping-pong 历史图）----
    // 深度重投影 + 邻域 AABB 钳制 + 历史混合，平滑几何锯齿/SSAO 噪点/雾抖动颗粒；
    // 首帧（ResetTaa 后）newFrame=1 直通重建历史，不采样未定义内容。
    bool taaActive = false;
    VkImageView taaOutView = VK_NULL_HANDLE;
    if (UseMsaa() && sceneDepthImage_ != VK_NULL_HANDLE)
    {
        taaActive = taaEnabled;
        // 每帧重定向 bright/composite 场景源：TAA 启用 → 刚写完的 TAA 输出；否则维持运动模糊输出
        taaOutView = (taaIndex_ & 1u) ? taaImageB_.View() : taaImageA_.View();
        const VkImageView postSceneView = taaActive ? taaOutView : mbImage_.View();
        VkDescriptorImageInfo sceneInfo{};
        sceneInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sceneInfo.imageView = postSceneView;
        sceneInfo.sampler = sampler_;
        VkWriteDescriptorSet sourceWrites[2]{};
        sourceWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sourceWrites[0].dstSet = brightDescSet_;
        sourceWrites[0].dstBinding = 0;
        sourceWrites[0].descriptorCount = 1;
        sourceWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sourceWrites[0].pImageInfo = &sceneInfo;
        sourceWrites[1] = sourceWrites[0];
        sourceWrites[1].dstSet = compositeDescSet_;
        vkUpdateDescriptorSets(device_, 2, sourceWrites, 0, nullptr);

        if (taaActive)
        {
            const VkFramebuffer taaTarget = (taaIndex_ & 1u) ? taaBFramebuffer_ : taaAFramebuffer_;
            const VkDescriptorSet taaSrc = (taaIndex_ & 1u) ? taaDescSetB_ : taaDescSetA_;
            beginPass(postRenderPass_, taaTarget, extent_);
            taaPipeline_->Bind(cmd);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, taaPipeline_->pipelineLayout, 0, 1, &taaSrc,
                                    0, nullptr);
            struct TaaParams
            {
                glm::mat4 reproj; // prevVP × inverse(currVP)（双方带抖动）
                float enabled;    // 1=启用（Pass 运行即 1）
                float feedback;   // 历史权重 [0,0.95]
                float newFrame;   // 1=首帧直通重建历史
                float jitterX;    // 当前帧裁剪空间抖动量
                float jitterY;
                float pad0;
            } tp{taaReproj_, 1.0f, glm::clamp(taaFeedback, 0.0f, 0.95f), (taaFrameCounter_ == 0) ? 1.0f : 0.0f,
                 taaJitter_.x, taaJitter_.y, 0.0f};
            static_assert(sizeof(TaaParams) == 88, "TaaParams 必须为 88 字节（mat4 + 6 float）");
            vkCmdPushConstants(cmd, taaPipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(tp), &tp);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
            ++taaFrameCounter_;
            taaIndex_ ^= 1u;
        }
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
    // 升级 26/27：b3 指向本帧最新适应亮度图（ping-pong 翻转），b4/b5 绑定雾阴影同源资源
    {
        const VkImageView adaptedView = (adaptIndex_ & 1u) ? adaptImageB_.View() : adaptImageA_.View();
        VkDescriptorImageInfo adaptedInfo{};
        adaptedInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        adaptedInfo.imageView = adaptedView;
        adaptedInfo.sampler = sampler_;
        VkWriteDescriptorSet writes[3]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = compositeDescSet_;
        writes[0].dstBinding = 3;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &adaptedInfo;
        uint32_t writeCount = 1;
        VkDescriptorBufferInfo fogUboInfo{};
        VkDescriptorImageInfo fogShadowInfo{};
        if (fogShadowLightUbo_ != VK_NULL_HANDLE && fogShadowView_ != VK_NULL_HANDLE)
        {
            fogUboInfo.buffer = fogShadowLightUbo_;
            fogUboInfo.offset = 0;
            fogUboInfo.range = LightUBO_ByteSize;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = compositeDescSet_;
            writes[1].dstBinding = 4;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[1].pBufferInfo = &fogUboInfo;
            // 阴影图 finalLayout 为 DEPTH_STENCIL_READ_ONLY（separateDepthStencilLayouts 未启用，
            // 不能用单面 DEPTH_READ_ONLY，VUID-03285），描述符 imageLayout 须与实际布局一致
            fogShadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            fogShadowInfo.imageView = fogShadowView_;
            fogShadowInfo.sampler = fogShadowSampler_ ? fogShadowSampler_ : sampler_;
            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = compositeDescSet_;
            writes[2].dstBinding = 5;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[2].pImageInfo = &fogShadowInfo;
            writeCount = 3;
        }
        vkUpdateDescriptorSets(device_, writeCount, writes, 0, nullptr);
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, compositePipeline_->pipelineLayout, 0, 1,
                            &compositeDescSet_, 0, nullptr);
    // 升级 25/26/27：布局与 pp_composite.frag.glsl 的 CompositeParams 严格一致（128 字节）
    struct CompositeParams
    {
        float bloomStrength;
        float exposure;
        float saturation;
        float contrast;
        float lift;
        float gain;
        float gamma;
        float fogQuality; // 步数(16/32/64)+0.5=雾中投影，0=关闭雾
        float fogDensity;
        float fogHeightFalloff;
        float fogBaseHeight;
        float fogScatter;
        float tanHalfFov;
        float aspect;
        float autoExposure;
        float keyValue;
        glm::vec3 fogTint;
        float vignetteIntensity;
        glm::vec3 camPos;
        float vignetteRadius;
        glm::vec3 sunL;
        float grainAmount;
        glm::vec3 camFwd;
        float grainTime;
    };
    static_assert(sizeof(CompositeParams) == 128, "CompositeParams 必须为 128 字节（与 shader 布局一致）");
    // 升级 27：雾质量编码——整数部分为步数，0.5 小数分量表示启用雾中投影（需资源已就绪）
    const float fogQuality = fogEnabled
                                 ? (glm::clamp(float(fogSteps), 4.0f, 64.0f) +
                                    ((fogShadowEnabled && fogShadowLightUbo_ != VK_NULL_HANDLE) ? 0.5f : 0.0f))
                                 : 0.0f;
    const CompositeParams comp{bloomStrength,
                               exposure,
                               gradeSaturation,
                               gradeContrast,
                               gradeLift,
                               gradeGain,
                               gradeGamma,
                               fogQuality,
                               fogDensity,
                               fogHeightFalloff,
                               fogBaseHeight,
                               fogScatter,
                               fogTanHalfFov_,
                               fogAspect_,
                               autoExposure ? 1.0f : 0.0f,
                               exposureKeyValue,
                               fogTint,
                               vignetteIntensity,
                               fogCamPos_,
                               vignetteRadius,
                               fogSunL_,
                               filmGrain,
                               fogCamFwd_,
                               grainTime_};
    vkCmdPushConstants(cmd, compositePipeline_->pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(comp), &comp);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // 升级 26：帧推进（适应 ping-pong 翻转 + 首帧 reset 标记消耗 + 颗粒动画时钟）
    ++frameCounter_;
    adaptIndex_ ^= 1u;
    grainTime_ += frameDelta_;
}
} // namespace BigHero::Render
