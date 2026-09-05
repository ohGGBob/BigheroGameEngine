#pragma once
#include "shader_loader.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
/// 图形管线创建配置：布局资源（描述符布局/推送常量）与渲染状态均可外部定制
struct GraphicsPipelineConfig
{
    std::vector<VkDescriptorSetLayout> setLayouts;
    std::vector<VkPushConstantRange> pushConstants;

    // 顶点输入绑定（支持多绑定，例如 [逐顶点绑定0, 逐实例绑定1] 实现实例化渲染）。
    // 为空表示无顶点缓冲（全屏三角形等场景）。
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;

    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    // Vulkan帧缓冲Y朝下：配合"投影Y翻转"使用CCW正面
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    bool depthTest = true;
    bool depthWrite = true;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
    // 光栅化采样数：需与渲染通道附件的采样数一致
    VkSampleCountFlagBits rasterSamples = VK_SAMPLE_COUNT_1_BIT;
    // 仅深度通道（阴影贴图等）：无颜色附件，跳过颜色混合状态
    bool depthOnly = false;
    // 颜色附件数量（多渲染目标 MRT 用，例如延迟渲染 GBuffer 写 3 张）
    uint32_t colorAttachmentCount = 1;
    // 所属子通道下标（多子通道渲染通道中，几何/光照分阶段）
    uint32_t subpass = 0;
    // Alpha 混合开关（粒子/半透明特效用），默认关闭以兼容既有不透明管线
    bool blendEnable = false;
};

/// 图形管线封装，RAII管理管线与管线布局
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
    GraphicsPipeline(VkDevice dev, VkRenderPass rp, ShaderModuleHandle vertModule, ShaderModuleHandle fragModule,
                     const GraphicsPipelineConfig& config)
        : device(dev), renderPass(rp), vertShader(std::move(vertModule)), fragShader(std::move(fragModule))
    {
        if (device == VK_NULL_HANDLE)
            throw std::runtime_error("GraphicsPipeline: 无效的逻辑设备");
        if (renderPass == VK_NULL_HANDLE)
            throw std::runtime_error("GraphicsPipeline: 无效的渲染通道");
        if (!vertShader.IsValid() || !fragShader.IsValid())
            throw std::runtime_error("GraphicsPipeline: 顶点/片段着色器模块无效");

        CreatePipelineLayout(config);
        CreatePipeline(config);
    }

    // 禁止拷贝：管线资源独占不可共享
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    // 移动语义：安全转移Vulkan资源所有权
    GraphicsPipeline(GraphicsPipeline&& other) noexcept { Swap(other); }
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
    ~GraphicsPipeline() { Release(); }

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

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
                                static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
    }

    [[nodiscard]] VkPipelineLayout GetLayout() const noexcept { return pipelineLayout; }
    [[nodiscard]] VkPipeline GetPipeline() const noexcept { return pipeline; }

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
        return device != VK_NULL_HANDLE && renderPass != VK_NULL_HANDLE && pipelineLayout != VK_NULL_HANDLE &&
               pipeline != VK_NULL_HANDLE && vertShader.IsValid() && fragShader.IsValid();
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

    /// 创建管线布局：外部提供的描述符布局+推送常量
    void CreatePipelineLayout(const GraphicsPipelineConfig& config)
    {
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(config.setLayouts.size());
        layoutInfo.pSetLayouts = config.setLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(config.pushConstants.size());
        layoutInfo.pPushConstantRanges = config.pushConstants.data();

        const VkResult res = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);
        if (res != VK_SUCCESS)
            throw std::runtime_error("GraphicsPipeline: 创建管线布局失败");
    }

    /// 构建完整图形管线：不透明三角形渲染，动态视口/裁剪
    void CreatePipeline(const GraphicsPipelineConfig& config)
    {
        const std::vector<VkPipelineShaderStageCreateInfo> shaderStages = {
            BuildShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShader, "main"),
            BuildShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShader, "main")};

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (!config.vertexBindings.empty())
        {
            vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(config.vertexBindings.size());
            vertexInput.pVertexBindingDescriptions = config.vertexBindings.data();
        }
        if (!config.vertexAttributes.empty())
        {
            vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(config.vertexAttributes.size());
            vertexInput.pVertexAttributeDescriptions = config.vertexAttributes.data();
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        // 视口/裁剪矩形为动态状态，运行时设置
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = nullptr;
        viewportState.scissorCount = 1;
        viewportState.pScissors = nullptr;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = config.cullMode;
        rasterizer.frontFace = config.frontFace;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = config.rasterSamples;
        multisample.sampleShadingEnable = VK_FALSE;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = config.depthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = config.depthWrite ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = config.depthCompareOp;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttach{};
        colorBlendAttach.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttach.blendEnable = config.blendEnable ? VK_TRUE : VK_FALSE;
        if (config.blendEnable)
        {
            // 标准预乘无关 Alpha 混合：src.a * src + (1-src.a) * dst
            colorBlendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttach.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttach.alphaBlendOp = VK_BLEND_OP_ADD;
        }

        // 多渲染目标：每个颜色附件复用同一套混合状态（GBuffer 等不需要混合）
        const uint32_t blendCount = std::max(config.colorAttachmentCount, 1u);
        std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(blendCount, colorBlendAttach);

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.logicOpEnable = VK_FALSE;
        colorBlend.attachmentCount = blendCount;
        colorBlend.pAttachments = blendAttachments.data();
        const std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

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
        pipelineInfo.pColorBlendState = config.depthOnly ? nullptr : &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = config.subpass;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

        const VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        if (res != VK_SUCCESS)
            throw std::runtime_error("GraphicsPipeline: 创建图形管线失败");
    }

    /// 内部辅助：构建单个着色器阶段结构体
    [[nodiscard]] VkPipelineShaderStageCreateInfo BuildShaderStage(VkShaderStageFlagBits stage,
                                                                   const ShaderModuleHandle& module,
                                                                   const char* entryName) const noexcept
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
} // namespace BigHero::Render
