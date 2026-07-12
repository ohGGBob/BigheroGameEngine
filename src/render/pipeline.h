#pragma once
#include "descriptor_set.h"
#include "shader_loader.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include <utility>
#include <cstdint>

namespace BigHero::Render
{
    /// 基础3D图形管线封装，适配当前双UBO描述符布局与着色器
    class GraphicsPipeline
    {
    public:
        VkDevice device = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;

        // RAII自动管理着色器模块生命周期
        ShaderModuleHandle vertShader;
        ShaderModuleHandle fragShader;

        /// 构造：创建管线布局 + 完整图形管线
        GraphicsPipeline(
            VkDevice dev,
            VkRenderPass rp,
            ShaderModuleHandle vertModule,
            ShaderModuleHandle fragModule,
            const DescriptorManager& descManager
        )
            : device(dev),
              renderPass(rp),
              vertShader(std::move(vertModule)),
              fragShader(std::move(fragModule))
        {
            if (device == VK_NULL_HANDLE)
                throw std::runtime_error("GraphicsPipeline: Invalid logical device handle");
            if (renderPass == VK_NULL_HANDLE)
                throw std::runtime_error("GraphicsPipeline: Invalid render pass handle");
            if (!vertShader.IsValid() || !fragShader.IsValid())
                throw std::runtime_error("GraphicsPipeline: Vertex/Fragment shader module invalid");

            CreatePipelineLayout(descManager);
            CreatePipeline();
        }

        // 禁止拷贝：管线资源独占不可共享
        GraphicsPipeline(const GraphicsPipeline&) = delete;
        GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

        // 移动语义：安全转移Vulkan资源所有权
        GraphicsPipeline(GraphicsPipeline&& other) noexcept
        {
            Swap(other);
        }
        GraphicsPipeline& operator=(GraphicsPipeline&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                Swap(other);
            }
            return *this;
        }

        /// 析构自动释放全部管线资源
        ~GraphicsPipeline()
        {
            Release();
        }

        /// 将图形管线绑定到命令缓冲区
        void Bind(VkCommandBuffer cmd) const noexcept
        {
            if (!IsValid() || cmd == VK_NULL_HANDLE)
                return;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        }

        /// 绑定描述符集集合到当前管线
        void BindDescriptorSets(VkCommandBuffer cmd, const std::vector<VkDescriptorSet>& sets) const
        {
            if (!IsValid() || cmd == VK_NULL_HANDLE || sets.empty())
                return;

            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout,
                0,
                static_cast<uint32_t>(sets.size()),
                sets.data(),
                0,
                nullptr
            );
        }

        /// 手动释放所有Vulkan管线相关资源
        void Release() noexcept
        {
            if (device == VK_NULL_HANDLE)
                return;

            if (pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(device, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
            if (pipelineLayout != VK_NULL_HANDLE)
            {
                vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
                pipelineLayout = VK_NULL_HANDLE;
            }

            vertShader.Release();
            fragShader.Release();

            device = VK_NULL_HANDLE;
            renderPass = VK_NULL_HANDLE;
        }

        /// 校验管线所有资源句柄是否有效
        [[nodiscard]] bool IsValid() const noexcept
        {
            return device != VK_NULL_HANDLE
                && renderPass != VK_NULL_HANDLE
                && pipelineLayout != VK_NULL_HANDLE
                && pipeline != VK_NULL_HANDLE
                && vertShader.IsValid()
                && fragShader.IsValid();
        }

    private:
        /// 交换两个管线对象的全部资源（移动语义辅助）
        void Swap(GraphicsPipeline& other) noexcept
        {
            std::swap(device, other.device);
            std::swap(renderPass, other.renderPass);
            std::swap(pipelineLayout, other.pipelineLayout);
            std::swap(pipeline, other.pipeline);
            std::swap(vertShader, other.vertShader);
            std::swap(fragShader, other.fragShader);
        }

        /// 创建管线布局，绑定set0相机UBO、set1光照UBO两套布局
        void CreatePipelineLayout(const DescriptorManager& descManager)
        {
            const std::vector<VkDescriptorSetLayout> setLayouts = {
                descManager.layoutCamera,
                descManager.layoutLight
            };

            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
            layoutInfo.pSetLayouts = setLayouts.data();
            layoutInfo.pushConstantRangeCount = 0;
            layoutInfo.pPushConstantRanges = nullptr;

            const VkResult res = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);
            if (res != VK_SUCCESS)
                throw std::runtime_error("GraphicsPipeline: Failed to create pipeline layout");
        }

        /// 构建完整基础3D图形管线，默认不透明三角形渲染配置
        void CreatePipeline()
        {
            // 1. 着色器阶段配置
            const std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
                BuildShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShader, "main"),
                BuildShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShader, "main")
            };

            // 2. 顶点输入（暂未绑定顶点缓冲，后续拓展）
            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInput.vertexBindingDescriptionCount = 0;
            vertexInput.pVertexBindingDescriptions = nullptr;
            vertexInput.vertexAttributeDescriptionCount = 0;
            vertexInput.pVertexAttributeDescriptions = nullptr;

            // 3. 图元装配：三角形列表
            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            inputAssembly.primitiveRestartEnable = VK_FALSE;

            // 4. 视口/裁剪矩形（动态状态，运行时设置）
            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = nullptr;
            viewportState.scissorCount = 1;
            viewportState.pScissors = nullptr;

            // 5. 光栅化：背面剔除、实体填充
            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.depthClampEnable = VK_FALSE;
            rasterizer.rasterizerDiscardEnable = VK_FALSE;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_FALSE;

            // 6. 多重采样：无MSAA
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            multisample.sampleShadingEnable = VK_FALSE;

            // 7. 深度测试：开启深度写入，近物体覆盖远物体
            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
            depthStencil.depthBoundsTestEnable = VK_FALSE;
            depthStencil.stencilTestEnable = VK_FALSE;

            // 8. 颜色混合：不透明物体直接覆盖帧缓冲
            VkPipelineColorBlendAttachmentState colorBlendAttach{};
            colorBlendAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                | VK_COLOR_COMPONENT_G_BIT
                | VK_COLOR_COMPONENT_B_BIT
                | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttach.blendEnable = VK_FALSE;

            VkPipelineColorBlendStateCreateInfo colorBlend{};
            colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlend.logicOpEnable = VK_FALSE;
            colorBlend.attachmentCount = 1;
            colorBlend.pAttachments = &colorBlendAttach;

            // 9. 动态状态：视口、裁剪矩形
            const std::vector<VkDynamicState> dynamicStates = {
                VK_DYNAMIC_STATE_VIEWPORT,
                VK_DYNAMIC_STATE_SCISSOR
            };
            VkPipelineDynamicStateCreateInfo dynamicState{};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
            dynamicState.pDynamicStates = dynamicStates.data();

            // 10. 管线总创建信息
            VkGraphicsPipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
            pipelineInfo.pStages = shaderStages.data();
            pipelineInfo.pVertexInputState = &vertexInput;
            pipelineInfo.pInputAssemblyState = &inputAssembly;
            pipelineInfo.pViewportState = &viewportState;
            pipelineInfo.pRasterizationState = &rasterizer;
            pipelineInfo.pMultisampleState = &multisample;
            pipelineInfo.pDepthStencilState = &depthStencil;
            pipelineInfo.pColorBlendState = &colorBlend;
            pipelineInfo.pDynamicState = &dynamicState;
            pipelineInfo.layout = pipelineLayout;
            pipelineInfo.renderPass = renderPass;
            pipelineInfo.subpass = 0;
            pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

            const VkResult res = vkCreateGraphicsPipelines(
                device,
                VK_NULL_HANDLE,
                1,
                &pipelineInfo,
                nullptr,
                &pipeline
            );
            if (res != VK_SUCCESS)
                throw std::runtime_error("GraphicsPipeline: Failed to create graphics pipeline");
        }

        /// 内部辅助：构建单个着色器阶段结构体，消除重复代码
        [[nodiscard]] VkPipelineShaderStageCreateInfo BuildShaderStage(
            VkShaderStageFlagBits stage,
            const ShaderModuleHandle& module,
            const char* entryName
        ) const noexcept
        {
            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = stage;
            stageInfo.module = module;
            stageInfo.pName = entryName;
            stageInfo.pSpecializationInfo = nullptr;
            return stageInfo;
        }
    };
}