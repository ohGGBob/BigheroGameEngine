#pragma once
// 音频引擎 RAII 封装：基于 miniaudio 高层 API（ma_engine），
// 自动管理音频设备、混音器、主音量。Sound 对象通过 ma_engine* 播放。
//
// 生命周期：AudioEngine 必须先于所有 Sound 构造、后于所有 Sound 析构。
// 设备初始化失败时 IsValid() 返回 false，所有 Sound 操作安全降级为空操作。

#include "miniaudio.h"

#include <cstdint>

namespace BigHero::Audio
{
class AudioEngine
{
  public:
    AudioEngine()
    {
        ma_result result = ma_engine_init(nullptr, &engine_);
        if (result != MA_SUCCESS)
            return;
        initialized_ = true;
        ma_engine_set_volume(&engine_, masterVolume_);
    }

    ~AudioEngine()
    {
        if (initialized_)
            ma_engine_uninit(&engine_);
    }

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // 主音量 0.0（静音）~ 1.0（原音量），可超过 1.0 放大（可能削波）
    void SetMasterVolume(float volume)
    {
        masterVolume_ = volume;
        if (initialized_)
            ma_engine_set_volume(&engine_, volume);
    }

    [[nodiscard]] float MasterVolume() const noexcept { return masterVolume_; }
    [[nodiscard]] ma_engine* Native() noexcept { return &engine_; }
    [[nodiscard]] bool IsValid() const noexcept { return initialized_; }

  private:
    ma_engine engine_{};
    bool initialized_ = false;
    float masterVolume_ = 1.0f;
};
} // namespace BigHero::Audio
