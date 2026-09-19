#pragma once
// 音频混音纯逻辑（S2 总线混音）：总线增益解算、静音优先级、3D 距离衰减曲线。
//
// 本头文件刻意不依赖 miniaudio / 引擎运行时（仅标准库），可离线单测：
// test_audio.cpp 以对拍表验证各曲线与解算规则；AudioEngine 在运行时按同一套
// 规则把状态落到 miniaudio 节点图上（端点音量 × ma_sound_group 音量）。
//
// 混音链路：final = Master(音量×静音) × Bus(音量×静音) × Source(音量) × 距离衰减
// 静音优先级最高：任一级 muted=true 时该级增益恒为 0（无视音量）。

#include <algorithm>
#include <cmath>

namespace BigHero::Audio
{
// 音量钳制域：负值视为 0，上限 4.0（允许轻度放大，防误操作炸音）。
inline constexpr float kMaxBusVolume = 4.0f;

inline float ClampVolume(float volume)
{
    return std::clamp(volume, 0.0f, kMaxBusVolume);
}

// 总线状态（纯数据）：独立音量 + 静音开关。
struct BusState
{
    float volume = 1.0f;
    bool muted = false;
};

// 单总线增益：静音优先级最高 —— muted 时无论音量多少，增益恒 0。
inline float BusGain(const BusState& bus)
{
    return bus.muted ? 0.0f : ClampVolume(bus.volume);
}

// 两级串联解算（Master × Bus）：任一级静音即静音，负音量按 0 处理。
inline float SolveGain(float masterVolume, bool masterMuted, const BusState& bus)
{
    if (masterMuted)
        return 0.0f;
    return ClampVolume(masterVolume) * BusGain(bus);
}

// 全链路解算（Master × Bus × 音源音量 × 距离衰减），供运行时与测试共用同一公式。
inline float SolveFinalGain(float masterVolume, bool masterMuted, const BusState& bus, float sourceVolume,
                            float attenuation)
{
    return SolveGain(masterVolume, masterMuted, bus) * ClampVolume(sourceVolume) * attenuation;
}

// 3D 距离衰减模型（S1 空间化）。
enum class AttenuationModel
{
    Inverse, // 反比（默认，miniaudio ma_attenuation_model_inverse 同族）
    Linear,  // 线性（OpenAL AL_LINEAR_DISTANCE_CLAMPED 风格）
};

// 反比衰减（规格公式）：
//   atten = min(1, minDist / max(dist, minDist)) ^ rolloff
// dist < minDist 时恒为 1（全音量半径）；rolloff=1 即经典 1/d，rolloff=2 即 1/d²，
// rolloff=0 不衰减。minDistance 必须 > 0（由 DistanceAttenuation 兜底，调用方应先 Normalize）。
inline float InverseDistanceAttenuation(float distance, float minDistance, float rolloff)
{
    const float d = std::max(distance, minDistance);
    const float base = minDistance / d; // d >= minDistance > 0 时 base ∈ (0, 1]
    return std::pow(base, rolloff);
}

// 线性衰减：minDistance 处 1，maxDistance 处 0，区间内线性，区间外钳制。
// maxDistance <= minDistance 时退化为阶跃（<= min 为 1，否则 0）。
inline float LinearDistanceAttenuation(float distance, float minDistance, float maxDistance)
{
    if (maxDistance <= minDistance)
        return distance <= minDistance ? 1.0f : 0.0f;
    const float d = std::clamp(distance, minDistance, maxDistance);
    return 1.0f - (d - minDistance) / (maxDistance - minDistance);
}

// 距离衰减统一入口：非法 minDistance（<=0）兜底为 1，避免除零/负指数域。
inline float DistanceAttenuation(AttenuationModel model, float distance, float minDistance, float maxDistance,
                                 float rolloff)
{
    if (minDistance <= 0.0f)
        minDistance = 1.0f;
    if (model == AttenuationModel::Linear)
        return LinearDistanceAttenuation(distance, minDistance, maxDistance);
    return InverseDistanceAttenuation(distance, minDistance, rolloff);
}
} // namespace BigHero::Audio
