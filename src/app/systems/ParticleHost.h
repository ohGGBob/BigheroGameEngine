#pragma once
// 阶段 3e：粒子子系统（自 Application 拆出）。
// 职责：CPU 粒子模拟（ParticleSystem）+ 编辑器实时调参/预设切换 + P 键爆发 +
//       GPU 实例缓冲上传与公告板管线的持有/重建（初始化与交换链重建共用）。
// 持 GPU 资源（ParticleBuffer/GraphicsPipeline）：在 Application 中须声明于 ctx_ 之后，
// 析构逆序保证 Vulkan 资源先于 Context 释放。字段公有（编辑器经指针直写）。

#include "game/EmitterPresets.h"
#include "game/ParticleSystem.h"
#include "render/Context.h"
#include "render/ParticleBuffer.h"
#include "render/pipeline.h"
#include "render/shader_loader.h"

#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace BigHero
{
class ParticleHost
{
  public:
    // 粒子公告板推送常量：视图投影矩阵 + 相机世界右/上轴（用于面向相机展开四边形）
    struct PushParticle
    {
        glm::mat4 viewProj;
        glm::vec3 camRight;
        float _p0 = 0.0f;
        glm::vec3 camUp;
        float _p1 = 0.0f;
    };
    static_assert(sizeof(PushParticle) == 96, "PushParticle 须为 96 字节（mat4 + 2*vec3+pad）");

    // ---- 模拟状态（编辑器经指针直写） ----
    Game::ParticleSystem system;
    bool enabled = true;         // 粒子系统总开关
    Game::Emitter emitterConfig; // 编辑器可实时调参的发射器配置（每帧写入 system）
    float gravity = -4.0f;       // 模拟重力 Y（编辑器可调）
    float damping = 0.4f;        // 速度阻尼（编辑器可调）
    int emitterPresetIndex = 0;     // 当前预设下标（编辑器下拉框）
    int prevEmitterPresetIndex = 0; // 边沿检测：切换预设时重建配置

    // ---- GPU 资源 ----
    Render::ParticleBuffer buffer;                    // GPU 实例缓冲
    std::vector<Render::ParticleInstance> scratch;    // 每帧复用，避免动态分配
    std::optional<Render::GraphicsPipeline> pipeline; // 公告板管线
    Render::GraphicsPipelineConfig config;            // 管线配置（交换链重建时复用）
    bool keyHeld = false;                             // P 键爆发边沿检测

    // 初始化：默认喷泉预设 + GPU 实例缓冲（容量与模拟池一致）
    void Init(const Context& ctx);

    // 创建/重建公告板管线（初始化与交换链重建共用；emplace 幂等覆盖旧管线）
    void CreatePipeline(VkDevice dev, VkRenderPass mainPass, VkSampleCountFlagBits rasterSamples);

    // 每帧推进：写回编辑器配置 → 模拟 → 生成实例 → 上传 GPU
    void Update(float dt, const Context& ctx);

    // 在指定世界坐标（相机注视点上方）触发一次粒子爆发
    void EmitBurst(const glm::vec3& origin);

    // 将编辑器配置写入 ParticleSystem（每帧/预设切换时）
    void ApplyConfig();
};
} // namespace BigHero
