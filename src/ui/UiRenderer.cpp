// 运行时 UI 批渲染器 GPU 实现：渲染通道/帧缓冲/管线/双槽位顶点缓冲与单 draw call 录制。
#include "ui/UiRenderer.h"

#include "core/Log.h"
#include "core/VkCheck.h"
#include "render/Context.h"
#include "render/Swapchain.h"
#include "render/shader_loader.h"

#include <cstring>
#include <stdexcept>

namespace BigHero::Ui
{
void UiRenderer::Init(const Context& ctx, const Swapchain& swapchain)
{
    Destroy();
    ctx_ = &ctx;
    device_ = ctx.Device();
    CreateRenderPass(swapchain.Format());
    CreateFramebuffers(swapchain);
    for (uint32_t slot = 0; slot < kMaxFrames; ++slot)
        vertexBuffers_[slot].Create(ctx, kInitialVertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    LOG_INFO("UI 批渲染器初始化完成（顶点缓冲 " << kMaxFrames << " 槽位 × " << kInitialVertexBytes / 1024 << " KB）");
}

void UiRenderer::CreateRenderPass(VkFormat format)
{
    // 与编辑器覆盖层同一约定：LOAD 场景/前置 UI 遗留颜色，finalLayout 保持 COLOR_ATTACHMENT_OPTIMAL，
    // 由随后的 ImGui 覆盖层通道（finalLayout=PRESENT）完成呈现布局转换
    VkAttachmentDescription color{};
    color.format = format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // 外部依赖：等待前置颜色写入（场景通道）完成，绘制后写对后续（覆盖层通道）可见
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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
    VK_CHECK(vkCreateRenderPass(device_, &passInfo, nullptr, &renderPass_), "创建 UI 渲染通道");
}

void UiRenderer::CreateFramebuffers(const Swapchain& swapchain)
{
    DestroyFramebuffers();
    framebuffers_.resize(swapchain.ImageCount());
    for (uint32_t i = 0; i < swapchain.ImageCount(); ++i)
    {
        const VkImageView attachment = swapchain.Views()[i];
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass_;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &attachment;
        fbInfo.width = swapchain.Extent().width;
        fbInfo.height = swapchain.Extent().height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffers_[i]), "创建 UI 帧缓冲");
    }
}

void UiRenderer::DestroyFramebuffers()
{
    if (device_ == VK_NULL_HANDLE)
        return;
    for (VkFramebuffer fb : framebuffers_)
        if (fb != VK_NULL_HANDLE)
            vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
}

void UiRenderer::CreatePipeline(VkDevice dev, VkDescriptorSetLayout atlasLayout, VkDescriptorSet atlasSet)
{
    if (renderPass_ == VK_NULL_HANDLE)
        throw std::runtime_error("UiRenderer::CreatePipeline: 渲染通道未初始化");
    atlasLayout_ = atlasLayout;
    atlasSet_ = atlasSet;

    Render::ShaderModuleHandle vert(dev, Render::ReadShaderFile("shaders/ui.vert.spv"));
    Render::ShaderModuleHandle frag(dev, Render::ReadShaderFile("shaders/ui.frag.spv"));

    // 顶点输入：单绑定逐顶点（pos/local/color/uv/rect）
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = kVertexStride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::vector<VkVertexInputAttributeDescription> attrs(5);
    attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, pos)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, local)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UiVertex, color)};
    attrs[3] = {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(UiVertex, uv)};
    attrs[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(UiVertex, rect)};

    config_.setLayouts = {atlasLayout};
    config_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec4)}};
    config_.vertexBindings = {binding};
    config_.vertexAttributes = attrs;
    config_.cullMode = VK_CULL_MODE_NONE;
    config_.depthTest = false;
    config_.depthWrite = false;
    config_.blendEnable = true; // 标准 Alpha 混合（pipeline.h 默认因子）
    config_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
    config_.colorAttachmentCount = 1;

    pipeline_.emplace(dev, renderPass_, std::move(vert), std::move(frag), config_);
}

void UiRenderer::OnSwapchainRecreated(const Swapchain& swapchain)
{
    if (ctx_ == nullptr || renderPass_ == VK_NULL_HANDLE)
        return;
    CreateFramebuffers(swapchain);
}

void UiRenderer::Destroy()
{
    if (device_ == VK_NULL_HANDLE)
        return;
    DestroyFramebuffers();
    pipeline_.reset();
    for (Buffer& vb : vertexBuffers_)
        vb.Destroy();
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
    ctx_ = nullptr;
    atlasLayout_ = VK_NULL_HANDLE;
    atlasSet_ = VK_NULL_HANDLE;
}

void UiRenderer::EnsureVertexCapacity(const Context& ctx, uint32_t frameSlot, VkDeviceSize bytes)
{
    if (frameSlot >= kMaxFrames)
        return;
    Buffer& vb = vertexBuffers_[frameSlot];
    if (vb.IsValid() && vb.Size() >= bytes)
        return;
    // 扩容：两帧在飞期间同槽位缓冲的 GPU 读已被帧栅栏保证结束（UploadVertices 在
    // DrawFrame 的栅栏等待之后、本帧录制之前调用），仍 WaitIdle 兜底防时序外调用
    ctx.WaitIdle();
    const VkDeviceSize newSize = bytes + kInitialVertexBytes / 2;
    vb.Create(ctx, newSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    LOG_INFO("UI 顶点缓冲扩容: 槽位 " << frameSlot << " → " << newSize / 1024 << " KB");
}

void UiRenderer::UploadVertices(const Context& ctx, uint32_t frameSlot, const UiVertex* data, size_t count)
{
    if (frameSlot >= kMaxFrames || data == nullptr || count == 0)
        return;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(count) * kVertexStride;
    EnsureVertexCapacity(ctx, frameSlot, bytes);
    vertexBuffers_[frameSlot].UploadData(ctx, data, bytes);
}

const Buffer& UiRenderer::VertexBuffer(uint32_t frameSlot) const
{
    return vertexBuffers_[frameSlot % kMaxFrames];
}

void UiRenderer::Record(VkCommandBuffer cmd, uint32_t imageIndex, uint32_t frameSlot, uint32_t vertexCount,
                        VkExtent2D extent)
{
    if (!IsValid() || cmd == VK_NULL_HANDLE || vertexCount == 0)
        return;
    if (imageIndex >= framebuffers_.size() || framebuffers_[imageIndex] == VK_NULL_HANDLE)
        return; // 交换链重建时序异常：本帧 UI 跳过（画面损失一帧，不崩溃）

    VkClearValue clear{}; // loadOp=LOAD，清除值不生效
    VkRenderPassBeginInfo passInfo{};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    passInfo.renderPass = renderPass_;
    passInfo.framebuffer = framebuffers_[imageIndex];
    passInfo.renderArea.offset = {0, 0};
    passInfo.renderArea.extent = extent;
    passInfo.clearValueCount = 1;
    passInfo.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

    pipeline_->Bind(cmd);
    if (atlasSet_ != VK_NULL_HANDLE)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->GetLayout(), 0, 1, &atlasSet_, 0,
                                nullptr);

    // 动态视口/裁剪（管线声明 VUID-07831/07832 要求绘制前显式设置）
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 屏幕像素 → NDC：ndc = pos * (2/w, 2/h) - (1, 1)（左上原点、y 向下直通 Vulkan NDC）
    const glm::vec4 scaleOffset(2.0f / static_cast<float>(extent.width), 2.0f / static_cast<float>(extent.height),
                                -1.0f, -1.0f);
    vkCmdPushConstants(cmd, pipeline_->GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec4), &scaleOffset);

    const Buffer& vb = VertexBuffer(frameSlot);
    VkBuffer handle = vb.Get();
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &handle, &offset);
    vkCmdDraw(cmd, vertexCount, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}
} // namespace BigHero::Ui
