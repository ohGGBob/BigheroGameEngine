#pragma once
// 粒子系统管理：CPU 模拟 + GPU 实例化公告板渲染 + 编辑器预设
// 原 Application 中 particleSystem_, particleEmitterConfig_, particleBuffer_, particlePipeline_ 等逻辑

#include "ISubSystem.h"
#include "game/ParticleSystem.h"
#include "game/EmitterPresets.h"
#include "render/ParticleBuffer.h"
#include "render/GraphicsPipeline.h"
#include "render/Context.h"

namespace BigHero::App
{

class ParticleSystem final : public ISubSystem
{
public:
    ParticleSystem() = default;

    [[nodiscard]] const char* Name() const noexcept override { return "ParticleSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return -20; }

    void Init(class Context& ctx) { ctx_ = &ctx; particleSystem_ = Game::ParticleSystem(4096); particleBuffer_.Create(*ctx_, 4096); }
    void Shutdown() override { particleBuffer_.Destroy(); particlePipeline_.reset(); }

    void Update(const FrameContext& frame) override { if (!enabled_) return; ApplyConfig(); particleSystem_.Update(frame.deltaTime); UploadToGPU(); }
    void PreRender(uint32_t frameIndex) override {}
    void OnSwapchainRecreated() override { if (particlePipeline_) particlePipeline_.reset(); }
    void OnRenderPassRecreated() override {}

    void ApplyConfig()
    {
        if (emitterPresetIndex_ != prevEmitterPresetIndex_)
        {
            const auto preset = Game::MakeEmitterPreset(emitterPresetIndex_);
            emitterConfig_ = preset.emitter;
            gravity_ = preset.gravity.y;
            damping_ = preset.damping;
            prevEmitterPresetIndex_ = emitterPresetIndex_;
        }
        particleSystem_.SetEmitter(emitterConfig_);
        particleSystem_.SetGravity(glm::vec3(0.0f, gravity_, 0.0f));
        particleSystem_.SetDamping(damping_);
    }

    void EmitBurst(const glm::vec3& pos)
    {
        Game::Emitter e = emitterConfig_;
        e.origin = pos;
        e.origin.y += 0.5f;
        particleSystem_.SetEmitter(e);
        particleSystem_.Emit(150);
    }

    void UploadToGPU()
    {
        const auto& particles = particleSystem_.GetParticles();
        particleScratch_.clear();
        particleScratch_.reserve(particles.size());
        for (const auto& p : particles)
        {
            if (!p.active) continue;
            Render::ParticleInstance inst;
            inst.position = p.position;
            inst.color = p.color;
            inst.size = p.size;
            inst.lifeRatio = p.life / p.maxLife;
            particleScratch_.push_back(inst);
        }
        particleBuffer_.Upload(particleScratch_);
    }

    void CreatePipeline(const Context& ctx, const VkRenderPass mainPass, VkSampleCountFlagBits sampleCount)
    {
        Render::ShaderModuleHandle pv(*ctx_.Device(), Render::ReadShaderFile("shaders/particle.vert.spv"));
        Render::ShaderModuleHandle pf(*ctx_.Device(), Render::ReadShaderFile("shaders/particle.frag.spv"));
        Render::GraphicsPipelineConfig cfg;
        cfg.setLayouts = {};
        cfg.vertexBindings = {Render::ParticleBuffer::GetBindingDesc()};
        cfg.vertexAttributes = Render::ParticleBuffer::GetAttrDesc();
        cfg.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushParticle)}};
        cfg.cullMode = VK_CULL_MODE_NONE;
        cfg.depthTest = true;
        cfg.depthWrite = false;
        cfg.blendEnable = true;
        cfg.rasterSamples = sampleCount_;
        particlePipeline_.emplace(*ctx_.Device(), mainPass, std::move(pv), std::move(pf), cfg);
        particleConfig_ = cfg;
    }

    void RecordParticleCmd(VkCommandBuffer cmd, uint32_t frameIndex, VkExtent2D extent) const
    {
        if (!enabled_ || !particlePipeline_ || particleSystem_.AliveCount() == 0)
            return;
        // 绑定管线、描述符、实例缓冲，绘制
    }

    void SetEnabled(bool v) { enabled_ = v; }
    [[nodiscard]] bool Enabled() const noexcept { return enabled_; }
    void SetEmitterPresetIndex(int idx) { emitterPresetIndex_ = idx; }
    [[nodiscard]] int EmitterPresetIndex() const noexcept { return emitterPresetIndex_; }
    void SetGravity(float g) { gravity_ = g; }
    [[nodiscard]] float Gravity() const noexcept { return gravity_; }
    void SetDamping(float d) { damping_ = d; }
    [[nodiscard]] float Damping() const noexcept { return damping_; }
    [[nodiscard]] const Game::ParticleSystem& System() const noexcept { return particleSystem_; }
    [[nodiscard]] const Game::Emitter& EmitterConfig() const noexcept { return emitterConfig_; }
    [[nodiscard]] const std::vector<Render::ParticleInstance>& Scratch() const noexcept { return particleScratch_; }
    [[nodiscard]] const std::optional<Render::GraphicsPipeline>& Pipeline() const noexcept { return particlePipeline_; }
    [[nodiscard]] const char* const* PresetNames() const noexcept { return Game::kEmitterPresetNames; }
    [[nodiscard]] size_t PresetCount() const noexcept { return Game::kEmitterPresetCount; }

    void SetContext(const Context& ctx) { ctx_ = &ctx; }

    [[nodiscard]] const char* Name() const noexcept override { return "ParticleSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return -20; }

private:
    const Context* ctx_ = nullptr;
    Game::ParticleSystem particleSystem_;
    Game::Emitter emitterConfig_;
    float gravity_ = -4.0f;
    float damping_ = 0.4f;
    bool enabled_ = true;
    int emitterPresetIndex_ = 0;
    int prevEmitterPresetIndex_ = 0;
    Render::ParticleBuffer particleBuffer_;
    std::optional<Render::GraphicsPipeline> particlePipeline_;
    Render::GraphicsPipelineConfig particleConfig_;
    std::vector<Render::ParticleInstance> particleScratch_;
    const Context* ctx_ = nullptr;
};

} // namespace BigHero::App