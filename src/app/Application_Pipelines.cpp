// Application 管线生命周期翻译单元：CreatePipelines（首次构建全部图形管线）、
// RebuildMainPipelines / RebuildDeferredPipelines（交换链重建时轻量重建）、
// UpdateGBufferSets（GBuffer 附件视图写入描述符）。
// 从 Application.cpp 拆出（2026-09 复评收尾：Application 主文件 ≤1200 行）。
#include "app/Application.h"

namespace BigHero
{

void Application::CreatePipelines()
{
    const VkDevice dev = ctx_.Device();
    const VkRenderPass mainPass = renderer_.GetRenderPass();
    const VkRenderPass deferredPass = renderer_.GetDeferredRenderPass();
    // 注意：光照管线必须使用独立的 lightingRenderPass_（subpass 0）。
    // deferredRenderPass_ 只有 1 个 subpass，以 subpass=1 创建管线是越界未定义行为（驱动段错误）
    const VkRenderPass lightingPass = renderer_.GetLightingRenderPass();

    const VkVertexInputBindingDescription vertexBinding = Scene::Vertex::getBindingDesc();
    const std::vector<VkVertexInputAttributeDescription> vertexAttributes = Scene::Vertex::getAttrDesc();
    const VkVertexInputBindingDescription instanceBinding = Render::InstanceBuffer::GetBindingDesc();
    const std::vector<VkVertexInputAttributeDescription> instanceAttributes = Render::InstanceBuffer::GetAttrDesc();

    auto mergedAttrs = [&]()
    {
        std::vector<VkVertexInputAttributeDescription> attrs = vertexAttributes;
        attrs.insert(attrs.end(), instanceAttributes.begin(), instanceAttributes.end());
        return attrs;
    };

    // ---- 主场景前向管线 ----
    {
        Render::ShaderModuleHandle vert(dev, Render::ReadShaderFile(kVertSpvPath));
        Render::ShaderModuleHandle frag(dev, Render::ReadShaderFile(kFragSpvPath));
        pipelineConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
        pipelineConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject)}};
        pipelineConfig_.vertexBindings = {vertexBinding, instanceBinding};
        pipelineConfig_.vertexAttributes = mergedAttrs();
        pipelineConfig_.rasterSamples = renderer_.SampleCount();
        pipeline_.emplace(dev, mainPass, std::move(vert), std::move(frag), pipelineConfig_);
    }

    // ---- 方向光阴影深度管线 ----
    {
        Render::ShaderModuleHandle sv(dev, Render::ReadShaderFile("shaders/shadow.vert.spv"));
        Render::ShaderModuleHandle sf(dev, Render::ReadShaderFile("shaders/shadow.frag.spv"));
        shadowConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushShadow)}};
        shadowConfig_.vertexBindings = {vertexBinding};
        shadowConfig_.vertexAttributes = vertexAttributes;
        shadowConfig_.cullMode = VK_CULL_MODE_FRONT_BIT;
        shadowConfig_.depthOnly = true;
        shadowPipeline_.emplace(dev, shadowMap_.GetRenderPass(), std::move(sv), std::move(sf), shadowConfig_);
    }

    // ---- 点光源立方体阴影深度管线 ----
    {
        Render::ShaderModuleHandle cv(dev, Render::ReadShaderFile("shaders/shadow_cube.vert.spv"));
        Render::ShaderModuleHandle cf(dev, Render::ReadShaderFile("shaders/shadow_cube.frag.spv"));
        cubeShadowConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight,
                                        descManager_.layoutCubeShadow}; // 着色器在 set=2 访问 PointShadowUBO
        cubeShadowConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushCubeShadow)}};
        cubeShadowConfig_.vertexBindings = {vertexBinding};
        cubeShadowConfig_.vertexAttributes = vertexAttributes;
        cubeShadowConfig_.cullMode = VK_CULL_MODE_FRONT_BIT;
        cubeShadowConfig_.depthOnly = true;
        cubeShadowPipeline_.emplace(dev, cubeShadowMap_.GetRenderPass(), std::move(cv), std::move(cf),
                                    cubeShadowConfig_);
    }

    // ---- 天空盒管线 ----
    {
        Render::ShaderModuleHandle kv(dev, Render::ReadShaderFile("shaders/skybox.vert.spv"));
        Render::ShaderModuleHandle kf(dev, Render::ReadShaderFile("shaders/skybox.frag.spv"));
        skyboxConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
        skyboxConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushSky)}};
        skyboxConfig_.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        skyboxConfig_.depthWrite = false;
        skyboxConfig_.cullMode = VK_CULL_MODE_NONE;
        skyboxConfig_.rasterSamples = renderer_.SampleCount();
        skyboxPipeline_.emplace(dev, mainPass, std::move(kv), std::move(kf), skyboxConfig_);
    }

    // ---- 延迟渲染：GBuffer 几何管线（MRT 写 3 张） ----
    {
        Render::ShaderModuleHandle gv(dev, Render::ReadShaderFile(kVertSpvPath));
        Render::ShaderModuleHandle gf(dev, Render::ReadShaderFile("shaders/gbuffer.frag.spv"));
        gbufferConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
        gbufferConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject)}};
        gbufferConfig_.vertexBindings = {vertexBinding, instanceBinding};
        gbufferConfig_.vertexAttributes = mergedAttrs();
        gbufferConfig_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
        gbufferConfig_.colorAttachmentCount = 3;
        gbufferConfig_.subpass = 0;
        gbufferConfig_.depthTest = true;
        gbufferConfig_.depthWrite = true;
        gbufferPipeline_.emplace(dev, deferredPass, std::move(gv), std::move(gf), gbufferConfig_);
    }

    // ---- 延迟渲染：全屏延迟光照管线（输入附件） ----
    {
        Render::ShaderModuleHandle lv(dev, Render::ReadShaderFile("shaders/deferred_light.vert.spv"));
        Render::ShaderModuleHandle lf(dev, Render::ReadShaderFile("shaders/deferred_light.frag.spv"));
        defLightConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight,
                                      descManager_.layoutGBufferInput, descManager_.layoutAO};
        defLightConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
        defLightConfig_.vertexBindings = {};
        defLightConfig_.vertexAttributes = {};
        defLightConfig_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
        defLightConfig_.colorAttachmentCount = 1;
        defLightConfig_.subpass = 0; // lightingRenderPass_ 仅有一个 subpass
        defLightConfig_.depthTest = false;
        defLightConfig_.depthWrite = false;
        lightingPipeline_.emplace(dev, lightingPass, std::move(lv), std::move(lf), defLightConfig_);
    }

    // ---- glTF 透明（BLEND）前向管线：标准 Alpha 混合、不写深度（与主管线同 pass） ----
    {
        Render::ShaderModuleHandle bv(dev, Render::ReadShaderFile(kVertSpvPath));
        Render::ShaderModuleHandle bf(dev, Render::ReadShaderFile(kFragSpvPath));
        gltfBlendConfig_ = pipelineConfig_; // 复制主场景配置（顶点输入/布局/采样数一致）
        gltfBlendConfig_.depthWrite = false;
        gltfBlendConfig_.blendEnable = true;
        gltfBlendPipeline_.emplace(dev, mainPass, std::move(bv), std::move(bf), gltfBlendConfig_);
    }

    // ---- 延迟透明叠加管线（transparentRenderPass_）：BLEND 标准混合，深度只读测试 ----
    {
        const VkRenderPass transparentPass = renderer_.GetTransparentRenderPass();
        Render::ShaderModuleHandle tv(dev, Render::ReadShaderFile(kVertSpvPath));
        Render::ShaderModuleHandle tf(dev, Render::ReadShaderFile(kFragSpvPath));
        transBlendConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
        transBlendConfig_.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushObject)}};
        transBlendConfig_.vertexBindings = {vertexBinding, instanceBinding};
        transBlendConfig_.vertexAttributes = mergedAttrs();
        transBlendConfig_.rasterSamples = VK_SAMPLE_COUNT_1_BIT;
        transBlendConfig_.colorAttachmentCount = 1;
        transBlendConfig_.depthTest = true;
        transBlendConfig_.depthWrite = false;
        transBlendConfig_.blendEnable = true;
        transBlendPipeline_.emplace(dev, transparentPass, std::move(tv), std::move(tf), transBlendConfig_);
    }

    // ---- 延迟加性自发光管线（transparentRenderPass_）：ONE/ONE 加性，补写光照 Pass 无法感知的自发光 ----
    {
        const VkRenderPass transparentPass = renderer_.GetTransparentRenderPass();
        Render::ShaderModuleHandle ev(dev, Render::ReadShaderFile(kVertSpvPath));
        Render::ShaderModuleHandle ef(dev, Render::ReadShaderFile(kFragSpvPath));
        transEmissiveConfig_ = transBlendConfig_; // 与 BLEND 管线同布局/顶点输入/深度状态
        transEmissiveConfig_.blendSrcColor = VK_BLEND_FACTOR_ONE;
        transEmissiveConfig_.blendDstColor = VK_BLEND_FACTOR_ONE;
        transEmissiveConfig_.blendSrcAlpha = VK_BLEND_FACTOR_ONE;
        transEmissiveConfig_.blendDstAlpha = VK_BLEND_FACTOR_ONE;
        transEmissivePipeline_.emplace(dev, transparentPass, std::move(ev), std::move(ef), transEmissiveConfig_);
    }

    // ---- 粒子实例化公告板管线（前向-only，Alpha 混合，不写深度；阶段 3e 移入 ParticleHost） ----
    particleHost_.CreatePipeline(dev, mainPass, renderer_.SampleCount());
}

void Application::RebuildMainPipelines()
{
    const VkDevice dev = ctx_.Device();
    const VkRenderPass mainPass = renderer_.GetRenderPass();

    Render::ShaderModuleHandle v(dev, Render::ReadShaderFile(kVertSpvPath));
    Render::ShaderModuleHandle f(dev, Render::ReadShaderFile(kFragSpvPath));
    pipelineConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
    pipeline_ = Render::GraphicsPipeline(dev, mainPass, std::move(v), std::move(f), pipelineConfig_);

    Render::ShaderModuleHandle sv(dev, Render::ReadShaderFile("shaders/skybox.vert.spv"));
    Render::ShaderModuleHandle sf(dev, Render::ReadShaderFile("shaders/skybox.frag.spv"));
    skyboxConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
    skyboxPipeline_ = Render::GraphicsPipeline(dev, mainPass, std::move(sv), std::move(sf), skyboxConfig_);

    // 粒子管线（交换链重建时一并重建；阶段 3e 移入 ParticleHost）
    particleHost_.CreatePipeline(dev, mainPass, renderer_.SampleCount());

    // glTF 透明（BLEND）前向管线（与主管线同 pass，随交换链重建）
    Render::ShaderModuleHandle bv(dev, Render::ReadShaderFile(kVertSpvPath));
    Render::ShaderModuleHandle bf(dev, Render::ReadShaderFile(kFragSpvPath));
    gltfBlendConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
    gltfBlendPipeline_ = Render::GraphicsPipeline(dev, mainPass, std::move(bv), std::move(bf), gltfBlendConfig_);
}

void Application::RebuildDeferredPipelines()
{
    const VkDevice dev = ctx_.Device();
    const VkRenderPass geometryPass = renderer_.GetDeferredRenderPass();
    const VkRenderPass lightingPass = renderer_.GetLightingRenderPass();

    Render::ShaderModuleHandle gv(dev, Render::ReadShaderFile(kVertSpvPath));
    Render::ShaderModuleHandle gf(dev, Render::ReadShaderFile("shaders/gbuffer.frag.spv"));
    gbufferConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight};
    gbufferPipeline_ = Render::GraphicsPipeline(dev, geometryPass, std::move(gv), std::move(gf), gbufferConfig_);

    Render::ShaderModuleHandle lv(dev, Render::ReadShaderFile("shaders/deferred_light.vert.spv"));
    Render::ShaderModuleHandle lf(dev, Render::ReadShaderFile("shaders/deferred_light.frag.spv"));
    defLightConfig_.setLayouts = {descManager_.layoutCamera, descManager_.layoutLight, descManager_.layoutGBufferInput,
                                  descManager_.layoutAO};
    lightingPipeline_ = Render::GraphicsPipeline(dev, lightingPass, std::move(lv), std::move(lf), defLightConfig_);

    // 延迟透明叠加管线（随 transparentRenderPass_ 重建）
    const VkRenderPass transparentPass = renderer_.GetTransparentRenderPass();
    Render::ShaderModuleHandle tv(dev, Render::ReadShaderFile(kVertSpvPath));
    Render::ShaderModuleHandle tf(dev, Render::ReadShaderFile(kFragSpvPath));
    transBlendPipeline_ = Render::GraphicsPipeline(dev, transparentPass, std::move(tv), std::move(tf),
                                                   transBlendConfig_);

    Render::ShaderModuleHandle ev(dev, Render::ReadShaderFile(kVertSpvPath));
    Render::ShaderModuleHandle ef(dev, Render::ReadShaderFile(kFragSpvPath));
    transEmissivePipeline_ = Render::GraphicsPipeline(dev, transparentPass, std::move(ev), std::move(ef),
                                                      transEmissiveConfig_);
}

void Application::UpdateGBufferSets()
{
    const uint32_t n = renderer_.GetSwapchain().ImageCount();
    for (uint32_t i = 0; i < n; ++i)
        descManager_.UpdateGBufferSet(i, renderer_.GBufferAlbedoView(i), renderer_.GBufferNormalView(i),
                                      renderer_.GBufferPositionView(i));
    // 分配 AO 描述符集（首次调用时）
    if (descManager_.aoSet == VK_NULL_HANDLE)
        descManager_.AllocateAOSet();
}

} // namespace BigHero
