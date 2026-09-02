#pragma once
// 粒子发射器预设（纯逻辑，无 GPU/窗口依赖，可离线单测）。
//
// 设计：
//   - EmitterPreset：一个完整可复用的粒子效果配方 = 发射器配置（Game::Emitter）+ 重力 + 阻尼。
//     重力/阻尼不属于 Emitter 本身（它们是 ParticleSystem 的模拟参数），故在此打包。
//   - 预设表：喷泉 / 爆发 / 烟雾 / 火花，覆盖从连续发射到手动爆发的典型用法。
//   - MakeEmitterPreset(idx)：按索引取预设，越界回退到 0 号（喷泉）。预设名由 EmitterPresetNames() 提供，
//     供编辑器下拉框直接复用。
//
// 该模块是升级 19 的核心（粒子编辑器：实时调参 + 预设），复用升级 17 的 Game::Emitter / ParticleSystem。

#include "game/ParticleSystem.h"

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace BigHero::Game
{
// 一个完整粒子效果配方
struct EmitterPreset
{
    Emitter emitter;       // 发射器配置（速率/初速度/寿命/尺寸/颜色/抖动…）
    glm::vec3 gravity{0.0f, -4.0f, 0.0f}; // 模拟用重力
    float damping = 0.4f;  // 速度阻尼（每秒衰减比例）
};

// 预设数量与名称（索引即下拉框下标）
inline const char* const kEmitterPresetNames[] = {"喷泉 Fountain", "爆发 Burst", "烟雾 Smoke", "火花 Spark"};
inline int EmitterPresetCount() noexcept { return static_cast<int>(sizeof(kEmitterPresetNames) / sizeof(kEmitterPresetNames[0])); }

// 按索引构造预设；idx 越界回退到 0 号（喷泉）。
inline EmitterPreset MakeEmitterPreset(int idx) noexcept
{
    EmitterPreset p;
    switch (idx)
    {
    case 0: // 喷泉：向上喷射、轻微扩散，连续/手动均可
    default:
        p.emitter.rate = 0.0f;
        p.emitter.origin = glm::vec3(0.0f, 1.5f, 0.0f);
        p.emitter.spawnRadius = 0.3f;
        p.emitter.initialVelocity = glm::vec3(0.0f, 3.5f, 0.0f);
        p.emitter.speed = 1.5f;
        p.emitter.lifetimeMin = 1.2f;
        p.emitter.lifetimeMax = 2.4f;
        p.emitter.sizeMin = 0.12f;
        p.emitter.sizeMax = 0.35f;
        p.emitter.color = glm::vec3(1.0f, 0.6f, 0.2f);
        p.emitter.jitter = 1.2f;
        p.gravity = glm::vec3(0.0f, -4.0f, 0.0f);
        p.damping = 0.4f;
        break;

    case 1: // 爆发：速率 0，靠手动 Emit 一次性喷发，强扩散、下坠快
        p.emitter.rate = 0.0f;
        p.emitter.origin = glm::vec3(0.0f, 1.5f, 0.0f);
        p.emitter.spawnRadius = 0.5f;
        p.emitter.initialVelocity = glm::vec3(0.0f, 3.0f, 0.0f);
        p.emitter.speed = 3.0f;
        p.emitter.lifetimeMin = 0.8f;
        p.emitter.lifetimeMax = 1.6f;
        p.emitter.sizeMin = 0.2f;
        p.emitter.sizeMax = 0.5f;
        p.emitter.color = glm::vec3(1.0f, 0.9f, 0.3f);
        p.emitter.jitter = 2.0f;
        p.gravity = glm::vec3(0.0f, -6.0f, 0.0f);
        p.damping = 0.2f;
        break;

    case 2: // 烟雾：低速上升、大尺寸、长寿命、低对比色，几乎无下坠（轻微上浮）
        p.emitter.rate = 30.0f;
        p.emitter.origin = glm::vec3(0.0f, 0.2f, 0.0f);
        p.emitter.spawnRadius = 0.6f;
        p.emitter.initialVelocity = glm::vec3(0.0f, 0.5f, 0.0f);
        p.emitter.speed = 0.4f;
        p.emitter.lifetimeMin = 2.5f;
        p.emitter.lifetimeMax = 4.0f;
        p.emitter.sizeMin = 0.3f;
        p.emitter.sizeMax = 0.8f;
        p.emitter.color = glm::vec3(0.7f, 0.7f, 0.75f);
        p.emitter.jitter = 0.5f;
        p.gravity = glm::vec3(0.0f, 0.3f, 0.0f);
        p.damping = 1.5f;
        break;

    case 3: // 火花：高发射率、极小尺寸、极短寿命、强下坠
        p.emitter.rate = 60.0f;
        p.emitter.origin = glm::vec3(0.0f, 0.5f, 0.0f);
        p.emitter.spawnRadius = 0.15f;
        p.emitter.initialVelocity = glm::vec3(0.0f, 1.0f, 0.0f);
        p.emitter.speed = 2.5f;
        p.emitter.lifetimeMin = 0.3f;
        p.emitter.lifetimeMax = 0.8f;
        p.emitter.sizeMin = 0.05f;
        p.emitter.sizeMax = 0.15f;
        p.emitter.color = glm::vec3(1.0f, 0.8f, 0.3f);
        p.emitter.jitter = 1.5f;
        p.gravity = glm::vec3(0.0f, -9.8f, 0.0f);
        p.damping = 0.1f;
        break;
    }
    return p;
}

// 便捷：仅取发射器配置（不含重力/阻尼）
inline Emitter MakeEmitter(int idx) noexcept { return MakeEmitterPreset(idx).emitter; }

} // namespace BigHero::Game
