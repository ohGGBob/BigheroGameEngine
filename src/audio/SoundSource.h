#pragma once
// SoundSource（S1 3D 音源）：组件级纯数据描述 —— 引擎侧不持有运行时状态，
// 由场景对象/粒子发射器等持有，经 AudioEngine::Play3D(id, source) 播放一次性空间化音效。
//
// 字段即语义：位置/速度/最小最大距离/滚降/循环/音量。经 IsValid 校验、Normalize 规整
// 后才交给 AudioEngine（非法值不进入 miniaudio 空间化器）。

#include "AudioMixer.h"

#include <glm/glm.hpp>

#include <cmath>

namespace BigHero::Audio
{
struct SoundSource
{
    glm::vec3 position{0.0f, 0.0f, 0.0f}; // 世界空间位置（绝对定位，ma_positioning_absolute）
    glm::vec3 velocity{0.0f, 0.0f, 0.0f}; // 世界空间速度（预留给多普勒/听感扩展，随组件透传）
    float minDistance = 1.0f;             // 全音量半径（米）：dist < minDistance 恒为 1
    float maxDistance = 60.0f;            // 线性模型衰减到 0 的距离（反比模型仅作钳制参考）
    float rolloff = 1.0f;                 // 反比模型滚降指数（0=不衰减，1=1/d，2=1/d²）
    bool looping = false;                 // 是否循环（一次性音效通常 false）
    float volume = 1.0f;                  // 音源自身音量（与总线增益串联，ClampVolume 域）

    // 数据校验：所有标量有限且处于合法域；位置/速度不含 NaN/Inf。
    [[nodiscard]] bool IsValid() const noexcept
    {
        return std::isfinite(minDistance) && minDistance > 0.0f && std::isfinite(maxDistance)
            && maxDistance >= minDistance && std::isfinite(rolloff) && rolloff >= 0.0f && std::isfinite(volume)
            && volume >= 0.0f && AllFinite(position) && AllFinite(velocity);
    }

    // 规整到合法域：非法字段钳回安全默认，合法字段保持不变（幂等）。
    void Normalize() noexcept
    {
        if (!std::isfinite(minDistance) || minDistance <= 0.0f)
            minDistance = 1.0f;
        if (!std::isfinite(maxDistance) || maxDistance < minDistance)
            maxDistance = minDistance;
        if (!std::isfinite(rolloff) || rolloff < 0.0f)
            rolloff = 1.0f;
        volume = std::isfinite(volume) ? ClampVolume(volume) : 1.0f;
        Sanitize(position);
        Sanitize(velocity);
    }

  private:
    [[nodiscard]] static bool AllFinite(const glm::vec3& v) noexcept
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    static void Sanitize(glm::vec3& v) noexcept
    {
        if (!std::isfinite(v.x))
            v.x = 0.0f;
        if (!std::isfinite(v.y))
            v.y = 0.0f;
        if (!std::isfinite(v.z))
            v.z = 0.0f;
    }
};
} // namespace BigHero::Audio
