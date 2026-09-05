#pragma once
// 声音资源 RAII 封装：基于 miniaudio ma_sound，支持从文件加载（WAV/MP3/FLAC 等）、
// 播放/停止/循环、音量控制。移动语义支持存入容器。
//
// 加载失败时 IsValid() 返回 false，Play/Stop 等操作为空操作，保证音频缺失时引擎不崩溃。

#include "AudioEngine.h"

#include <string>

namespace BigHero::Audio
{
class Sound
{
  public:
    Sound() = default;

    ~Sound() { Destroy(); }

    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;

    Sound(Sound&& other) noexcept { MoveFrom(other); }
    Sound& operator=(Sound&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            MoveFrom(other);
        }
        return *this;
    }

    // 从音频文件加载（miniaudio 支持 WAV/MP3/FLAC/OGG 等）。
    // looping: 是否循环播放。engine 必须在本 Sound 生命周期内保持有效。
    bool Load(AudioEngine& engine, const char* path, bool looping = false)
    {
        Destroy();
        if (!engine.IsValid())
            return false;
        ma_result result = ma_sound_init_from_file(engine.Native(), path, looping ? MA_SOUND_FLAG_LOOPING : 0, nullptr,
                                                   nullptr, &sound_);
        if (result != MA_SUCCESS)
            return false;
        initialized_ = true;
        looping_ = looping;
        return true;
    }

    void Play()
    {
        if (!initialized_)
            return;
        ma_sound_start(&sound_);
    }

    void Stop()
    {
        if (!initialized_)
            return;
        ma_sound_stop(&sound_);
        ma_sound_seek_to_pcm_frame(&sound_, 0);
    }

    void SetVolume(float volume)
    {
        if (!initialized_)
            return;
        ma_sound_set_volume(&sound_, volume);
    }

    void SetLooping(bool looping)
    {
        if (!initialized_)
            return;
        looping_ = looping;
        ma_sound_set_looping(&sound_, looping ? MA_TRUE : MA_FALSE);
    }

    [[nodiscard]] bool IsPlaying() const { return initialized_ && ma_sound_is_playing(&sound_) == MA_TRUE; }

    [[nodiscard]] bool IsValid() const noexcept { return initialized_; }
    [[nodiscard]] bool IsLooping() const noexcept { return looping_; }

  private:
    void Destroy()
    {
        if (initialized_)
        {
            ma_sound_uninit(&sound_);
            initialized_ = false;
        }
    }

    void MoveFrom(Sound& other) noexcept
    {
        sound_ = other.sound_;
        initialized_ = other.initialized_;
        looping_ = other.looping_;
        other.initialized_ = false;
    }

    ma_sound sound_{};
    bool initialized_ = false;
    bool looping_ = false;
};
} // namespace BigHero::Audio
