#include "app/systems/ParticleHost.h"

#include "core/Log.h"

namespace BigHero
{
void ParticleHost::Init(const Context& ctx)
{
    // ---- 粒子系统：默认喷泉预设（升级 19：编辑器可实时切换/调参，P 键触发爆发）----
    const auto preset = Game::MakeEmitterPreset(0);
    emitterConfig = preset.emitter;
    gravity = preset.gravity.y;
    damping = preset.damping;
    emitterPresetIndex = 0;
    prevEmitterPresetIndex = 0;
    ApplyConfig();

    // GPU 实例缓冲：容量与模拟池一致
    buffer.Create(ctx, system.Capacity());
    LOG_INFO("粒子系统初始化: 容量 " << system.Capacity()
                                     << " 预设=" << Game::kEmitterPresetNames[emitterPresetIndex]);
}

void ParticleHost::CreatePipeline(VkDevice dev, VkRenderPass mainPass, VkSampleCountFlagBits rasterSamples)
{
    Render::ShaderModuleHandle pv(dev, Render::ReadShaderFile("shaders/particle.vert.spv"));
    Render::ShaderModuleHandle pf(dev, Render::ReadShaderFile("shaders/particle.frag.spv"));
    config.setLayouts = {}; // 公告板无需描述符集
    config.vertexBindings = {Render::ParticleBuffer::GetBindingDesc()};
    config.vertexAttributes = Render::ParticleBuffer::GetAttrDesc();
    config.pushConstants = {VkPushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushParticle)}};
    config.cullMode = VK_CULL_MODE_NONE;
    config.depthTest = true;
    config.depthWrite = false;
    config.blendEnable = true; // 标准 Alpha 混合（见 pipeline.h）
    config.rasterSamples = rasterSamples;
    pipeline.emplace(dev, mainPass, std::move(pv), std::move(pf), config);
}

void ParticleHost::EmitBurst(const glm::vec3& origin)
{
    Game::Emitter e = system.GetEmitter();
    e.origin = origin;
    e.origin.y += 0.5f; // 在相机注视点上方一点爆发
    system.SetEmitter(e);
    system.Emit(150);
    LOG_INFO("粒子爆发: 存活 " << system.AliveCount() << " / " << system.Capacity());
}

void ParticleHost::ApplyConfig()
{
    // 预设下标变化时，从预设表重建发射器/重力/阻尼；否则沿用编辑器实时微调后的配置。
    if (emitterPresetIndex != prevEmitterPresetIndex)
    {
        const auto preset = Game::MakeEmitterPreset(emitterPresetIndex);
        emitterConfig = preset.emitter;
        gravity = preset.gravity.y;
        damping = preset.damping;
        prevEmitterPresetIndex = emitterPresetIndex;
    }
    system.SetEmitter(emitterConfig);
    system.SetGravity(glm::vec3(0.0f, gravity, 0.0f));
    system.SetDamping(damping);
}

void ParticleHost::Update(float dt)
{
    if (!enabled)
        return;
    ApplyConfig(); // 每帧把编辑器最新配置写入模拟器（实时调参）
    system.Update(dt);

    const auto& parts = system.GetParticles();
    scratch.clear();
    scratch.reserve(parts.size());
    for (const auto& p : parts)
    {
        if (!p.active)
            continue;
        Render::ParticleInstance inst{};
        inst.position = p.position;
        inst.size = p.size;
        // 按剩余寿命比例做淡出（frag 已做圆形软边，这里调亮度）
        const float fade = glm::clamp(p.life / p.maxLife, 0.0f, 1.0f);
        inst.color = p.color * fade;
        scratch.push_back(inst);
    }
    // GPU 上传交由 Application 登记 FrameStaging（帧内瞬态拷贝），此处只生成实例数据
}
} // namespace BigHero
