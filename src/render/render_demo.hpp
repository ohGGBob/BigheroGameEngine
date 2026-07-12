#pragma once
#include "render_pass.h"
#include "pipeline.h"
#include "descriptor_set.h"
#include "ubo_buffer.h"
#include "shader_loader.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <string>

namespace BigHero::Render
{
    /// 完整渲染管线使用演示
    class RenderDemo
    {
    public:
        // 渲染基础资源
        VkDevice logicalDevice;
        VkPhysicalDevice physicalDevice;
        uint32_t graphicsQueueFamily;
        VkQueue graphicsQueue;

        RenderPass mainRenderPass;
        DescriptorManager descManager;

        // UBO缓冲
        UboBuffer<CameraUBO> cameraUbo;
        UboBuffer<LightUBO> lightUbo;

        // 着色器与图形管线
        ShaderModuleHandle vertShader;
        ShaderModuleHandle fragShader;
        GraphicsPipeline mainPipeline;

        RenderDemo(
            VkDevice dev,
            VkPhysicalDevice physDev,
            uint32_t queueFamilyIdx,
            VkQueue gQueue,
            VkFormat surfaceFormat,
            VkFormat depthFormat,
            const std::string& vertSpirvPath,
            const std::string& fragSpirvPath
        )
            : logicalDevice(dev),
              physicalDevice(physDev),
              graphicsQueueFamily(queueFamilyIdx),
              graphicsQueue(gQueue),
              mainRenderPass(dev, surfaceFormat, depthFormat),
              descManager(dev),
              cameraUbo(dev, physDev, queueFamilyIdx),
              lightUbo(dev, physDev, queueFamilyIdx),
              vertShader(dev, ReadShaderFile(vertSpirvPath)),
              fragShader(dev, ReadShaderFile(fragSpirvPath)),
              mainPipeline(dev, mainRenderPass, std::move(vertShader), std::move(fragShader), descManager)
        {
            // 分配一套描述符集（set0相机UBO + set1光照UBO）
            descManager.AllocateSet();

            // 初始化默认UBO数据
            InitDefaultUboData();
        }

        /// 每一帧更新相机与光照UBO
        void UpdateFrameUBO(const CameraUBO& camData, const LightUBO& lightData)
        {
            cameraUbo.Update(camData);
            lightUbo.Update(lightData);

            // 将缓冲写入描述符集
            descManager.UpdateSet(0, 0, cameraUbo);
            descManager.UpdateSet(1, 0, lightUbo);
        }

        /// 录制基础渲染命令
        void RecordRenderCommands(VkCommandBuffer cmd, VkFramebuffer frameBuf, VkExtent2D extent)
        {
            // 开始渲染通道
            VkRenderPassBeginInfo renderBegin{};
            renderBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderBegin.renderPass = mainRenderPass;
            renderBegin.framebuffer = frameBuf;
            renderBegin.renderArea.offset = {0, 0};
            renderBegin.renderArea.extent = extent;

            const VkClearValue clearColor = {{0.1f, 0.1f, 0.15f, 1.0f}};
            const VkClearValue clearDepth = {{1.0f, 0, 0, 0}};
            renderBegin.clearValueCount = 2;
            renderBegin.pClearValues = &clearColor;

            vkCmdBeginRenderPass(cmd, &renderBegin, VK_SUBPASS_CONTENTS_INLINE);

            // 绑定管线与描述符集
            mainPipeline.Bind(cmd);
            mainPipeline.BindDescriptorSets(cmd, descManager.GetSets());

            // 设置动态视口与裁剪
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(extent.width);
            viewport.height = static_cast<float>(extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{{0,0}, extent};
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // 此处后续添加绘制几何体指令 vkCmdDraw
            vkCmdDraw(cmd, 3, 1, 0, 0);

            vkCmdEndRenderPass(cmd);
        }

    private:
        /// 初始化默认相机、光照参数
        void InitDefaultUboData()
        {
            CameraUBO cam{};
            cam.view = glm::lookAt(glm::vec3(0, 3, 10), glm::vec3(0,0,0), glm::vec3(0,1,0));
            cam.proj = glm::perspective(glm::radians(60.0f), 16.0f/9.0f, 0.1f, 1000.0f);
            cameraUbo.Update(cam);

            LightUBO light{};
            light.lightDir = glm::vec3(0.5f, -1.0f, -0.3f);
            light.lightColor = glm::vec3(1.0f, 0.95f, 0.8f);
            light.cameraPos = glm::vec3(0,3,10);
            light.ambientFactor = 0.12f;
            light.specPower = 32.0f;
            light.specStrength = 1.0f;
            lightUbo.Update(light);

            // 首次更新描述符
            descManager.UpdateSet(0, 0, cameraUbo);
            descManager.UpdateSet(1, 0, lightUbo);
        }
    };
}