#pragma once
// 音频引擎 RAII 封装：基于 miniaudio 高层 API（ma_engine），
// 自动管理音频设备、混音器、主音量。Sound 对象通过 ma_engine* 播放。
//
// 生命周期：AudioEngine 必须先于所有 Sound 构造、后于所有 Sound 析构。
// 设备初始化失败时 IsValid() 返回 false，所有 Sound 操作安全降级为空操作。
//
// S1 3D 空间化 + S2 总线混音（2026-09）：
//   - 总线：Master（引擎端点音量）→ {Music, SFX}（ma_sound_group 节点），
//     每总线独立音量/静音；BGM 挂 Music，音效挂 SFX。
//   - 3D：监听器每帧经 UpdateListener(Pod) 同步；Play3D 以绝对定位 + 内置
//     ma_spatializer 播放一次性音效（距离衰减/滚降/全音量半径）。
//   - 音效资源：启动时以代码生成 PCM 内存源（Click/Spawn/Impact），无外部资产依赖。
//   - 降级：CI 无声卡 → ma_engine_init 失败 → 本类全部操作为空操作，不崩溃。

#include "AudioMixer.h"
#include "SoundSource.h"
#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace BigHero::Audio
{
// 混音总线（S2）：Master = 引擎端点（SetMasterVolume），Music/SFX = 端点下的
// ma_sound_group 子节点。节点图：Sound → Bus group → endpoint → device。
enum class Bus : uint32_t
{
    Music = 0,
    Sfx = 1,
};

// 内置程序化音效（S1 音效资源）：启动时代码生成 PCM 内存源（单声道 f32，引擎采样率），
// 不依赖任何外部音频资产文件。
enum class SfxId : uint32_t
{
    Click = 0,  // 短促点击：1.4kHz 正弦 + 指数衰减（~60ms）
    Spawn = 1,  // "爆发生成"：320→920Hz 上扫 + 二次谐波（~180ms，人物生成演示）
    Impact = 2, // 冲击：130Hz 低频体感 + 确定性噪声瞬态（~160ms，立方体生成演示）
    Count = 3
};

// 监听器状态（S1 3D）：Pod 参数 —— AudioEngine 不反向依赖 Window/相机类型，
// 由应用层每帧从活跃相机（位置/朝向）填充后传入。
struct AudioListenerState
{
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

class AudioEngine
{
  public:
    AudioEngine()
    {
        ma_result result = ma_engine_init(nullptr, &engine_);
        if (result != MA_SUCCESS)
            return; // 无音频设备（CI）：优雅降级，IsValid()==false
        initialized_ = true;
        ApplyMasterVolume();
        InitBuses();
        BuildProceduralSfx();
    }

    ~AudioEngine()
    {
        if (!initialized_)
            return;
        DestroyProceduralSfx();
        UninitBuses();
        ma_engine_uninit(&engine_);
    }

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // ---- Master（引擎端点） ----
    // 主音量 0.0（静音）~ 1.0（原音量），可超过 1.0 放大（可能削波）
    void SetMasterVolume(float volume)
    {
        masterVolume_ = volume;
        ApplyMasterVolume();
    }

    [[nodiscard]] float MasterVolume() const noexcept { return masterVolume_; }

    // Master 静音开关（与音量独立，静音优先级最高，见 AudioMixer.h）
    void SetMasterMuted(bool muted) noexcept
    {
        masterMuted_ = muted;
        ApplyMasterVolume();
    }

    [[nodiscard]] bool MasterMuted() const noexcept { return masterMuted_; }

    // ---- 总线（S2）：每总线独立音量/静音，静音优先级高于音量 ----
    void SetBusVolume(Bus bus, float volume)
    {
        BusState& state = busStates_[BusIndex(bus)];
        state.volume = volume;
        ApplyBusVolume(bus);
    }

    [[nodiscard]] float BusVolume(Bus bus) const noexcept { return busStates_[BusIndex(bus)].volume; }

    void SetBusMuted(Bus bus, bool muted)
    {
        busStates_[BusIndex(bus)].muted = muted;
        ApplyBusVolume(bus);
    }

    [[nodiscard]] bool BusMuted(Bus bus) const noexcept { return busStates_[BusIndex(bus)].muted; }

    // ---- 监听器（S1 3D）：每帧从应用层 Pod 参数同步（位置/朝向/世界上方向） ----
    void UpdateListener(const AudioListenerState& listener)
    {
        if (!initialized_)
            return;
        const glm::vec3 forward = NormalizedOr(listener.forward, glm::vec3(0.0f, 0.0f, -1.0f));
        const glm::vec3 up = NormalizedOr(listener.up, glm::vec3(0.0f, 1.0f, 0.0f));
        ma_engine_listener_set_position(&engine_, 0, listener.position.x, listener.position.y, listener.position.z);
        ma_engine_listener_set_direction(&engine_, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&engine_, 0, up.x, up.y, up.z);
    }

    // ---- 播放（S1） ----
    // 一次性 2D 音效：SFX 总线，关闭空间化（UI 提示音等）。
    bool PlaySfx(SfxId id) { return PlayInternal(id, SoundSource{}, /*spatial=*/false); }

    // 一次性 3D 空间化音效：SFX 总线 + 绝对定位 + 内置 ma_spatializer（距离衰减/滚降）。
    bool Play3D(SfxId id, const SoundSource& source) { return PlayInternal(id, source, /*spatial=*/true); }

    // Sound 路由用：返回总线节点（未初始化时返回 nullptr = 引擎端点兜底）
    [[nodiscard]] ma_sound_group* BusGroup(Bus bus) noexcept
    {
        if (!busesInited_)
            return nullptr;
        return &buses_[BusIndex(bus)];
    }

    [[nodiscard]] ma_engine* Native() noexcept { return &engine_; }
    [[nodiscard]] bool IsValid() const noexcept { return initialized_; }
    [[nodiscard]] bool HasProceduralSfx() const noexcept { return sfxInited_; }

  private:
    // ---- 内置程序化音效（S1 音效资源）：代码生成 PCM 内存源 ----
    // 方案选型：直接在内存生成 PCM（ma_audio_buffer 引用外部数据，零拷贝），
    // 不写临时 wav 文件 —— 无磁盘 IO、无资产清理问题、参数可调可测。
    static constexpr uint32_t kVoicesPerSfx = 4; // 每音效复用 voice 数（round-robin 抢占）
    static constexpr float kTwoPi = 6.28318530717958647692f;

    struct SfxVoice
    {
        ma_audio_buffer buffer{}; // 引用 pcm 数据（不拷贝），生命周期随 AudioEngine
        ma_sound sound{};
        bool inited = false;
    };

    struct SfxProgram
    {
        std::vector<float> pcm;                       // 单声道 f32 PCM（引擎采样率）
        std::array<SfxVoice, kVoicesPerSfx> voices{}; // 复用 voice（round-robin 抢占）
        uint32_t nextVoice = 0;
    };

    static constexpr size_t BusIndex(Bus bus) noexcept { return static_cast<size_t>(bus); }

    void ApplyMasterVolume()
    {
        if (!initialized_)
            return;
        ma_engine_set_volume(&engine_, masterMuted_ ? 0.0f : ClampVolume(masterVolume_));
    }

    void InitBuses()
    {
        busStates_ = {BusState{}, BusState{}};
        // pParentGroup = nullptr：挂到引擎端点（Master 音量之下）
        busesInited_ = ma_sound_group_init(&engine_, 0, nullptr, &buses_[BusIndex(Bus::Music)]) == MA_SUCCESS
            && ma_sound_group_init(&engine_, 0, nullptr, &buses_[BusIndex(Bus::Sfx)]) == MA_SUCCESS;
    }

    void UninitBuses()
    {
        if (!busesInited_)
            return;
        ma_sound_group_uninit(&buses_[BusIndex(Bus::Music)]);
        ma_sound_group_uninit(&buses_[BusIndex(Bus::Sfx)]);
        busesInited_ = false;
    }

    void ApplyBusVolume(Bus bus)
    {
        if (!busesInited_)
            return;
        const size_t index = BusIndex(bus);
        ma_sound_group_set_volume(&buses_[index], BusGain(busStates_[index]));
    }

    void BuildProceduralSfx()
    {
        ma_uint32 sampleRate = ma_engine_get_sample_rate(&engine_);
        if (sampleRate == 0)
            sampleRate = 44100;
        GenerateClick(sampleRate, sfx_[size_t(SfxId::Click)].pcm);
        GenerateSpawn(sampleRate, sfx_[size_t(SfxId::Spawn)].pcm);
        GenerateImpact(sampleRate, sfx_[size_t(SfxId::Impact)].pcm);

        bool anyVoice = false;
        for (auto& prog : sfx_)
        {
            if (prog.pcm.empty())
                continue;
            for (uint32_t v = 0; v < kVoicesPerSfx; ++v)
                anyVoice = InitVoice(prog, v) || anyVoice;
        }
        sfxInited_ = anyVoice;
    }

    bool InitVoice(SfxProgram& prog, uint32_t voiceIndex)
    {
        SfxVoice& voice = prog.voices[voiceIndex];
        const ma_audio_buffer_config config = ma_audio_buffer_config_init(
            ma_format_f32, /*channels=*/1, static_cast<ma_uint64>(prog.pcm.size()), prog.pcm.data(), nullptr);
        if (ma_audio_buffer_init(&config, &voice.buffer) != MA_SUCCESS)
            return false;
        // 挂 SFX 总线；总线未初始化时传 nullptr = 引擎端点（Master）兜底
        if (ma_sound_init_from_data_source(&engine_, &voice.buffer, 0, BusGroup(Bus::Sfx), &voice.sound) != MA_SUCCESS)
        {
            ma_audio_buffer_uninit(&voice.buffer);
            return false;
        }
        ma_sound_set_positioning(&voice.sound, ma_positioning_absolute);
        ma_sound_set_spatialization_enabled(&voice.sound, MA_TRUE);
        voice.inited = true;
        return true;
    }

    bool PlayInternal(SfxId id, const SoundSource& source, bool spatial)
    {
        if (!initialized_ || !sfxInited_)
            return false;
        if (!spatial && source.looping)
            return false; // 2D 复用池不支持循环（会永久占用 voice）
        SfxProgram& prog = sfx_[size_t(id)];
        const uint32_t index = prog.nextVoice;
        prog.nextVoice = (index + 1) % kVoicesPerSfx; // round-robin：旧 voice 直接抢占（一次性短音效语义）
        ma_sound* sound = &prog.voices[index].sound;

        SoundSource src = source;
        src.Normalize();

        ma_sound_stop(sound); // 抢占仍在发声的旧实例
        ma_sound_seek_to_pcm_frame(sound, 0);
        ma_sound_set_looping(sound, src.looping ? MA_TRUE : MA_FALSE);
        ma_sound_set_volume(sound, src.volume);
        ma_sound_set_spatialization_enabled(sound, spatial ? MA_TRUE : MA_FALSE);
        if (spatial)
        {
            ma_sound_set_position(sound, src.position.x, src.position.y, src.position.z);
            ma_sound_set_min_distance(sound, src.minDistance);
            ma_sound_set_max_distance(sound, src.maxDistance);
            ma_sound_set_rolloff(sound, src.rolloff);
        }
        return ma_sound_start(sound) == MA_SUCCESS;
    }

    void DestroyProceduralSfx()
    {
        for (auto& prog : sfx_)
        {
            for (auto& voice : prog.voices)
            {
                if (!voice.inited)
                    continue;
                ma_sound_uninit(&voice.sound);
                ma_audio_buffer_uninit(&voice.buffer);
                voice.inited = false;
            }
            prog.pcm.clear();
        }
        sfxInited_ = false;
    }

    // 头尾淡入淡出，避免波形起止爆音
    static void ApplyFades(std::vector<float>& pcm, ma_uint32 sampleRate)
    {
        if (pcm.empty())
            return;
        const size_t fadeIn = std::min(pcm.size(), static_cast<size_t>(static_cast<float>(sampleRate) * 0.003f));
        for (size_t i = 0; i < fadeIn; ++i)
            pcm[i] *= static_cast<float>(i) / static_cast<float>(fadeIn);
        const size_t fadeOut = std::min(pcm.size(), static_cast<size_t>(static_cast<float>(sampleRate) * 0.005f));
        for (size_t i = 0; i < fadeOut; ++i)
            pcm[pcm.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fadeOut);
    }

    // 点击：1.4kHz 正弦 + 指数衰减（~60ms）
    static void GenerateClick(ma_uint32 sampleRate, std::vector<float>& out)
    {
        const float duration = 0.06f;
        const size_t frames = static_cast<size_t>(duration * static_cast<float>(sampleRate));
        out.resize(frames);
        for (size_t i = 0; i < frames; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
            out[i] = 0.8f * std::exp(-t * 90.0f) * std::sin(kTwoPi * 1400.0f * t);
        }
        ApplyFades(out, sampleRate);
    }

    // "爆发生成"：320→920Hz 上扫 + 二次谐波（~180ms）
    static void GenerateSpawn(ma_uint32 sampleRate, std::vector<float>& out)
    {
        const float duration = 0.18f;
        const size_t frames = static_cast<size_t>(duration * static_cast<float>(sampleRate));
        out.resize(frames);
        float phase = 0.0f;
        for (size_t i = 0; i < frames; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
            const float freq = 320.0f + 600.0f * (t / duration);
            phase += kTwoPi * freq / static_cast<float>(sampleRate);
            const float envelope = std::exp(-t * 14.0f);
            out[i] = 0.6f * envelope * (std::sin(phase) + 0.3f * std::sin(2.0f * phase));
        }
        ApplyFades(out, sampleRate);
    }

    // 冲击：130Hz 低频体感 + 确定性噪声瞬态（~160ms），tanh 软限幅
    static void GenerateImpact(ma_uint32 sampleRate, std::vector<float>& out)
    {
        const float duration = 0.16f;
        const size_t frames = static_cast<size_t>(duration * static_cast<float>(sampleRate));
        out.resize(frames);
        uint32_t seed = 0x1234567u; // 确定性 LCG 噪声（生成可重复）
        for (size_t i = 0; i < frames; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
            seed = seed * 1664525u + 1013904223u;
            const float noise = static_cast<float>(seed >> 8) * (2.0f / static_cast<float>(1u << 24)) - 1.0f;
            const float body = std::exp(-t * 26.0f) * std::sin(kTwoPi * 130.0f * t);
            const float transient = std::exp(-t * 180.0f) * noise * 0.6f;
            out[i] = std::tanh(0.9f * body + transient);
        }
        ApplyFades(out, sampleRate);
    }

    static glm::vec3 NormalizedOr(const glm::vec3& v, const glm::vec3& fallback) noexcept
    {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            return fallback;
        const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
        if (len2 <= 1e-12f)
            return fallback;
        return v * (1.0f / std::sqrt(len2));
    }

    ma_engine engine_{};
    std::array<ma_sound_group, 2> buses_{}; // [Music, Sfx]
    std::array<BusState, 2> busStates_{};
    std::array<SfxProgram, size_t(SfxId::Count)> sfx_{};
    bool busesInited_ = false;
    bool sfxInited_ = false;
    bool initialized_ = false;
    float masterVolume_ = 1.0f;
    bool masterMuted_ = false;
};
} // namespace BigHero::Audio
