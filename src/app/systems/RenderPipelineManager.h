#pragma once
// 渲染管线管理：所有 GraphicsPipeline 创建/重建/配置集中管理
// 原 Application 中 CreatePipelines、RebuildMainPipelines、RebuildDeferredPipelines 等逻辑

#include "ISubSystem.h"
#include "render/Renderer.h"
#include "render/DescriptorManager.h"
#include "render/ShaderModule.h"
#include "render/GraphicsPipeline.h"
#include "render/GraphicsPipelineConfig.h"
#include "render/Context.h"
#include "scene/Vertex.h"
#include "render/InstanceBuffer.h"
#include "render/ParticleBuffer.h"
#include <glm/glm.hpp>
#include <optional>

namespace BigHero::App
{

struct PushShadow
{
    glm::mat4 lightSpace;
    glm::mat4 model;
};

struct PushCubeShadow
{
    glm::mat4 model;
    glm::vec4 faceIndex;  // x = face index (0..5)
};

struct PushSky
{
    glm::mat4 invViewProj;
};

struct PushParticle
{
    glm::mat4 viewProj;
    glm::vec3 camRight;
    float _p0 = 0.0f;
    glm::vec3 camUp;
    float _p1 = 0.0f;
};

class RenderPipelineManager final : public ISubSystem
{
public:
    RenderPipelineManager() = default;

    [[nodiscard]] const char* Name() const noexcept override { return "RenderPipelineManager"; }
    [[nodiscard]] int Priority() const noexcept override { return 0; }

    void Init(const Context& ctx, const Renderer& renderer, const DescriptorManager& descManager)
    {
        ctx_ = &ctx;
        renderer_ = &renderer;
        descManager_ = &descManager;
    }

    void Shutdown() override
    {
        pipeline_.reset();
        shadowPipeline_.reset();
        cubeShadowPipeline_.reset();
        skyboxPipeline_.reset();
        gbufferPipeline_.reset();
        lightingPipeline_.reset();
        particlePipeline_.reset();
        compositePipeline_.reset();
    }

    void Update(const FrameContext&) override {}
    void PreRender(uint32_t) override {}
    void OnSwapchainRecreated() override { RebuildMainPipelines(); RebuildDeferredPipelines(); }
    void OnRenderPassRecreated() override {}

    void CreatePipelines()
    {
        const VkDevice dev = ctx_->Device();
        const VkRenderPass mainPass = renderer_->GetRenderPass();
        const VkRenderPass deferredPass = renderer_->GetDeferredRenderPass();
        const VkSampleCountFlagBits sampleCount = renderer_->SampleCount();

        const VkVertexInputBindingDescription vertexBinding = Scene::Vertex::getBindingDesc();
        const auto vertexAttributes = Scene::Vertex::getAttrDesc();
        const auto instanceBinding = Render::InstanceBuffer::GetBindingDesc();
        const auto instanceAttributes = Render::InstanceBuffer::GetAttrDesc();

        auto mergedAttrs = [&]()
        {
            std::vector<VkVertexInputAttributeDescription> attrs = vertexAttributes;
            attrs.insert(attrs.end(), instanceAttributes.begin(), instanceAttributes.end());
            return attrs;
        };

        // 主场景前向管线
        {
            Render::ShaderModuleHandle vert(*ctx_->Device(), Render::ReadShaderFile(kVertSpvPath));
            Render::ShaderModuleHandle frag(*ctx_->Device(), Render::ReadShaderFile(kFragSpvPath));
            pipelineConfig_.setLayouts = {descManager_->layoutCamera, descManager_->layoutLight};
            pipelineConfig_.vertexBindings = {vertexBinding, instanceBinding};
            pipelineConfig_.vertexAttributes = mergedAttrs();
            pipelineConfig_.rasterSamples = renderer_->SampleCount();
            pipeline_.emplace(dev, mainPass, std::move(vert), std::move(frag), pipelineConfig_);
        }

        // 方向光阴影深度管线
        {
            Render::ShaderModuleHandle sv(dev, Render::ReadShaderFile("shaders/shadow.vert.spv"));
            Render::ShaderModuleHandle sf(dev, Render::ReadShaderFile("shaders/shadow.frag.spv"));
            shadowConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushShadow)}};
            shadowConfig_.vertexBindings = {vertexBinding};
            shadowConfig_.vertexAttributes = vertexAttributes;
            shadowConfig_.cullMode = VK_CULL_MODE_FRONT_BIT;
            shadowConfig_.depthOnly = true;
            shadowPipeline_.emplace(dev, renderer_->GetShadowRenderPass(), std::move(sv), std::move(sf), shadowConfig_);
        }

        // 点光源立方体阴影深度管线
        {
            Render::ShaderModuleHandle cv(dev, Render::ReadShaderFile("shaders/shadow_cube.vert.spv"));
            Render::ShaderModuleHandle cf(dev, Render::ReadShaderFile("shaders/shadow_cube.frag.spv"));
            cubeShadowConfig_.setLayouts = {descManager_->layoutCubeShadow};
            cubeShadowConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushCubeShadow)}};
            cubeShadowConfig_.vertexBindings = {vertexBinding};
            cubeShadowConfig_.vertexAttributes = vertexAttributes;
            cubeShadowConfig_.cullMode = VK_CULL_MODE_FRONT_BIT;
            cubeShadowConfig_.depthOnly = true;
            cubeShadowPipeline_.emplace(dev, renderer_->GetCubeShadowRenderPass(), std::move(cv), std::move(cf), cubeShadowConfig_);
        }

        // 天空盒管线
        {
            Render::ShaderModuleHandle kv(dev, Render::ReadShaderFile("shaders/skybox.vert.spv"));
            Render::ShaderModuleHandle kf(dev, Render::ReadShaderFile("shaders/skybox.frag.spv"));
            skyboxConfig_.setLayouts = {descManager_->layoutCamera, descManager_->layoutLight};
            skyboxConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushSky)}};
            skyboxConfig_.depthCompareOp = VK_COMPARE_OP_ALWAYS;
            skyboxConfig_.depthWrite = false;
            skyboxConfig_.cullMode = VK_CULL_MODE_NONE;
            skyboxConfig_.rasterSamples = renderer_->SampleCount();
            skyboxPipeline_.emplace(dev, mainPass, std::move(kv), std::move(kf), skyboxConfig_);
        }

        // 延迟渲染：GBuffer 几何管线 (MRT)
        {
            Render::ShaderModuleHandle gv(dev, Render::ReadShaderFile(kVertSpvPath));
            Render::ShaderModuleHandle gf(dev, Render::ReadShaderFile("shaders/gbuffer.frag.spv"));
            gbufferConfig_.setLayouts = {descManager_->layoutCamera, descManager_->layoutLight};
            gbufferConfig_.vertexBindings = {vertexBinding, instanceBinding};
            gbufferConfig_.vertexAttributes = mergedAttrs();
            gbufferConfig_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
            gbufferConfig_.colorAttachmentCount = 3;
            gbufferConfig_.subpass = 0;
            gbufferConfig_.depthTest = true;
            gbufferConfig_.depthWrite = true;
            gbufferPipeline_.emplace(dev, renderer_->GetDeferredRenderPass(), std::move(gv), std::move(gf), gbufferConfig_);
        }

        // 延迟渲染：全屏延迟光照管线 (输入附件)
        {
            Render::ShaderModuleHandle lv(dev, Render::ReadShaderFile("shaders/deferred_light.vert.spv"));
            Render::ShaderModuleHandle lf(dev, Render::ReadShaderFile("shaders/deferred_light.frag.spv"));
            defLightConfig_.setLayouts = {descManager_->layoutCamera, descManager_->layoutLight, descManager_->layoutGBufferInput};
            defLightConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
            defLightConfig_.vertexBindings = {};
            defLightConfig_.vertexAttributes = {};
            defLightConfig_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
            defLightConfig_.colorAttachmentCount = 1;
            defLightConfig_.subpass = 1;
            defLightConfig_.depthTest = false;
            defLightConfig_.depthWrite = false;
            lightingPipeline_.emplace(dev, renderer_->GetDeferredRenderPass(), std::move(lv), std::move(lf), defLightConfig_);
        }

        // 粒子实例化公告板管线 (前向-only，Alpha 混合，不写深度)
        {
            Render::ShaderModuleHandle pv(dev, Render::ReadShaderFile("shaders/particle.vert.spv"));
            Render::ShaderModuleHandle pf(dev, Render::ReadShaderFile("shaders/particle.frag.spv"));
            particleConfig_.setLayouts = {};
            particleConfig_.vertexBindings = {Render::ParticleBuffer::GetBindingDesc()};
            particleConfig_.vertexAttributes = Render::ParticleBuffer::GetAttrDesc();
            particleConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushParticle)}};
            particleConfig_.cullMode = VK_CULL_MODE_NONE;
            particleConfig_.depthTest = true;
            particleConfig_.depthWrite = false;
            particleConfig_.blendEnable = true;
            particleConfig_.rasterSamples = renderer_->SampleCount();
            particlePipeline_.emplace(dev, mainPass, std::move(pv), std::move(pf), particleConfig_);
        }

        // 延迟合成管线 (GBuffer/光照/后处理合成到交换链)
        {
            Render::ShaderModuleHandle cv(dev, Render::ReadShaderFile("shaders/deferred_light.vert.spv"));
            Render::ShaderModuleHandle cf(dev, Render::ReadShaderFile("shaders/deferred_composite.frag.spv"));
            compositeConfig_.setLayouts = {descManager_->layoutComposite};
            compositeConfig_.vertexBindings = {};
            compositeConfig_.vertexAttributes = {};
            compositeConfig_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
            compositeConfig_.depthTest = false;
            compositeConfig_.depthWrite = false;
            compositePipeline_.emplace(dev, renderer_->GetCompositeRenderPass(), std::move(cv), std::move(cf), compositeConfig_);
        }
    }

    void RebuildMainPipelines()
    {
        pipeline_.reset();
        shadowPipeline_.reset();
        cubeShadowPipeline_.reset();
        skyboxPipeline_.reset();
        particlePipeline_.reset();
        CreatePipelines();
    }

    void RebuildDeferredPipelines()
    {
        gbufferPipeline_.reset();
        lightingPipeline_.reset();
        compositePipeline_.reset();
        CreatePipelines();
    }

    // 访问器
    [[nodiscard]] const std::optional<Render::GraphicsPipeline>& Pipeline() const noexcept { return pipeline_; }
    [[nodiscard]] const std::optional<Render::GraphicsPipeline>& ShadowPipeline() const noexcept { return shadowPipeline_; }
    [[nodiscard]] const std::optional<Render::GraphicsPipeline>& CubeShadowPipeline() const noexcept { return cubeShadowPipeline_; }
    [[nodiscard]] const std::optional<Render::GraphicsPipeline>& SkyboxPipeline() const noexcept { return skyboxPipeline_; }
    [[nodiscard]] const std::optional<Render::GraphicsPipeline>& GBufferPipeline() const noexcept { return gbufferPipeline_; }
    [[nodiscard]] const std::optional<Render::GraphicsPipeline>& LightingPipeline() const noexcept { return lightingPipeline_; }
    [[nodiscard]] const std::optional<Render::GraphicsPipeline>& ParticlePipeline() const noexcept { return particlePipeline_; }
    [[nodiscard]] const std::optional<Render::GraphicsPipeline>& CompositePipeline() const noexcept { return compositePipeline_; }

    [[nodiscard]] const char* Name() const noexcept override { return "RenderPipelineManager"; }
    [[nodiscard]] int Priority() const noexcept override { return 0; }

private:
    const Context* ctx_ = nullptr;
    const Renderer* renderer_ = nullptr;
    const DescriptorManager* descManager_ = nullptr;

    Render::GraphicsPipelineConfig pipelineConfig_;
    Render::GraphicsPipelineConfig shadowConfig_;
    Render::GraphicsPipelineConfig cubeShadowConfig_;
    Render::GraphicsPipelineConfig skyboxConfig_;
    Render::GraphicsPipelineConfig gbufferConfig_;
    Render::GraphicsPipelineConfig defLightConfig_;
    Render::GraphicsPipelineConfig particleConfig_;
    Render::GraphicsPipelineConfig compositeConfig_;

    std::optional<Render::GraphicsPipeline> pipeline_;
    std::optional<Render::GraphicsPipeline> shadowPipeline_;
    std::optional<Render::GraphicsPipeline> cubeShadowPipeline_;
    std::optional<Render::GraphicsPipeline> skyboxPipeline_;
    std::optional<Render::GraphicsPipeline> gbufferPipeline_;
    std::optional<Render::GraphicsPipeline> lightingPipeline_;
    std::optional<Render::GraphicsPipeline> particlePipeline_;
    std::optional<Render::GraphicsPipeline> compositePipeline_;

    static constexpr const char* kVertSpvPath = "shaders/vert.spv";
    static constexpr const char* kFragSpvPath = "shaders/frag.spv";
};

} // namespace BigHero::App