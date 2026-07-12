#pragma once
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <vector>
#include <utility>
#include <cstdint>

namespace BigHero::Render
{
    /// 基础颜色+深度双附件渲染通道封装
    class RenderPass
    {
    public:
        VkDevice device = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;

        RenderPass(VkDevice dev, VkFormat colorFormat, VkFormat depthFormat)
            : device(dev)
        {
            if (device == VK_NULL_HANDLE)
                throw std::runtime_error("RenderPass: Invalid logical device");
            CreateRenderPass(colorFormat, depthFormat);
        }

        // 禁止拷贝
        RenderPass(const RenderPass&) = delete;
        RenderPass& operator=(const RenderPass&) = delete;

        // 移动语义
        RenderPass(RenderPass&& other) noexcept
        {
            Swap(other);
        }
        RenderPass& operator=(RenderPass&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                Swap(other);
            }
            return *this;
        }

        ~RenderPass()
        {
            Release();
        }

        /// 释放渲染通道资源
        void Release() noexcept
        {
            if (device == VK_NULL_HANDLE) return;
            if (renderPass != VK_NULL_HANDLE)
            {
                vkDestroyRenderPass(device, renderPass, nullptr);
                renderPass = VK_NULL_HANDLE;
            }
            device = VK_NULL_HANDLE;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return device != VK_NULL_HANDLE && renderPass != VK_NULL_HANDLE;
        }

        [[nodiscard]] operator VkRenderPass() const noexcept
        {
            return renderPass;
        }

    private:
        void Swap(RenderPass& other) noexcept
        {
            std::swap(device, other.device);
            std::swap(renderPass, other.renderPass);
        }

        void CreateRenderPass(VkFormat colorFormat, VkFormat depthFormat)
        {
            // 颜色附件描述
            VkAttachmentDescription colorAtt{};
            colorAtt.format = colorFormat;
            colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            // 深度附件描述
            VkAttachmentDescription depthAtt{};
            depthAtt.format = depthFormat;
            depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            // 附件引用
            VkAttachmentReference colorRef{};
            colorRef.attachment = 0;
            colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthRef{};
            depthRef.attachment = 1;
            depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            // 子通道配置
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;
            subpass.pDepthStencilAttachment = &depthRef;

            // 子通道依赖，同步显示与渲染
            VkSubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            std::vector<VkAttachmentDescription> attachments = {colorAtt, depthAtt};
            VkRenderPassCreateInfo passInfo{};
            passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            passInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            passInfo.pAttachments = attachments.data();
            passInfo.subpassCount = 1;
            passInfo.pSubpasses = &subpass;
            passInfo.dependencyCount = 1;
            passInfo.pDependencies = &dependency;

            const VkResult res = vkCreateRenderPass(device, &passInfo, nullptr, &renderPass);
            if (res != VK_SUCCESS)
                throw std::runtime_error("RenderPass: Create render pass failed");
        }
    };
}