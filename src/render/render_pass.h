#pragma once
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
/// 颜色+深度渲染通道封装，支持多重采样（MSAA）与解析附件
/// samples>1：颜色/深度附件为MSAA图像，解析到交换链图像后呈现
/// samples==1：颜色附件直接落在交换链图像上（退化为无MSAA路径）
class RenderPass
{
  public:
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;

    RenderPass() = default;

    RenderPass(VkDevice dev, VkFormat colorFormat, VkFormat depthFormat,
               VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT)
        : device(dev)
    {
        if (device == VK_NULL_HANDLE)
            throw std::runtime_error("RenderPass: 无效的逻辑设备");
        CreateRenderPass(colorFormat, depthFormat, samples);
    }

    // 延迟创建：交换链重建时可能需要重新指定附件格式
    void Create(VkDevice dev, VkFormat colorFormat, VkFormat depthFormat,
                VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT)
    {
        if (renderPass != VK_NULL_HANDLE)
            throw std::runtime_error("RenderPass::Create: 已存在渲染通道，请先Release");
        device = dev;
        if (device == VK_NULL_HANDLE)
            throw std::runtime_error("RenderPass: 无效的逻辑设备");
        CreateRenderPass(colorFormat, depthFormat, samples);
    }

    // 禁止拷贝
    RenderPass(const RenderPass&) = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    // 移动语义
    RenderPass(RenderPass&& other) noexcept { Swap(other); }
    RenderPass& operator=(RenderPass&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            Swap(other);
        }
        return *this;
    }

    ~RenderPass() { Release(); }

    /// 释放渲染通道资源
    void Release() noexcept
    {
        if (device == VK_NULL_HANDLE)
            return;
        if (renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }
        device = VK_NULL_HANDLE;
    }

    [[nodiscard]] bool IsValid() const noexcept { return device != VK_NULL_HANDLE && renderPass != VK_NULL_HANDLE; }

    [[nodiscard]] operator VkRenderPass() const noexcept { return renderPass; }

  private:
    void Swap(RenderPass& other) noexcept
    {
        std::swap(device, other.device);
        std::swap(renderPass, other.renderPass);
    }

    void CreateRenderPass(VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits samples)
    {
        const bool useMsaa = samples != VK_SAMPLE_COUNT_1_BIT;

        // 场景通道结束后颜色统一留在COLOR_ATTACHMENT_OPTIMAL布局，
        // 由UI覆盖层通道（loadOp=LOAD）绘制界面并转换为PRESENT_SRC
        VkAttachmentDescription colorAtt{};
        colorAtt.format = colorFormat;
        colorAtt.samples = samples;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAtt{};
        depthAtt.format = depthFormat;
        depthAtt.samples = samples;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // 解析附件在附件数组中的槽位
        VkAttachmentDescription resolveAtt{};
        VkAttachmentReference resolveRef{};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkAttachmentDescription attachments[3]{};
        uint32_t attachmentCount = 2;
        attachments[0] = colorAtt;
        attachments[1] = depthAtt;
        if (useMsaa)
        {
            resolveAtt.format = colorFormat;
            resolveAtt.samples = VK_SAMPLE_COUNT_1_BIT;
            resolveAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            resolveAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            resolveAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            resolveAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            resolveAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            resolveAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachments[2] = resolveAtt;
            resolveRef.attachment = 2;
            resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            subpass.pResolveAttachments = &resolveRef;
            attachmentCount = 3;
        }

        // 子通道依赖，同步显示与渲染
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        passInfo.attachmentCount = attachmentCount;
        passInfo.pAttachments = attachments;
        passInfo.subpassCount = 1;
        passInfo.pSubpasses = &subpass;
        passInfo.dependencyCount = 1;
        passInfo.pDependencies = &dependency;

        const VkResult res = vkCreateRenderPass(device, &passInfo, nullptr, &renderPass);
        if (res != VK_SUCCESS)
            throw std::runtime_error("RenderPass: 创建渲染通道失败");
    }
};
} // namespace BigHero::Render
