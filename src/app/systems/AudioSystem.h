#pragma once
// 音频系统：miniaudio 封装
// 原 Application 中 audioEngine_, bgm_ 等逻辑

#include "ISubSystem.h"
#include "audio/AudioEngine.h"
#include "audio/Sound.h"
#include <filesystem>

namespace BigHero::App
{

class AudioSystem final : public ISubSystem
{
public:
    AudioSystem() = default;

    [[nodiscard]] const char* Name() const noexcept override { return "AudioSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return -5; }

    void Init() override
    {
        if (audioEngine_.IsValid())
        {
            audioEngine_.SetMasterVolume(0.5f);
            const char* kBgmPath = "assets/audio/bgm.wav";
            if (std::filesystem::exists(kBgmPath))
            {
                if (bgm_.Load(audioEngine_, kBgmPath, true))
                {
                    bgm_.SetVolume(0.4f);
                    bgm_.Play();
                    LOG_INFO("背景音乐已播放: " << kBgmPath);
                }
                else
                {
                    LOG_WARN("背景音乐加载失败: " << kBgmPath);
                }
            }
            else
            {
                LOG_INFO("未找到背景音乐 " << kBgmPath << "，音频系统已就绪");
            }
        }
        else
        {
            LOG_WARN("音频设备初始化失败，音频功能已禁用");
        }
    }

    void Shutdown() override
    {
        bgm_.Stop();
    }

    void Update(const FrameContext& frame) override
    {
        // 音频引擎通常不需要每帧更新
    }

    void PreRender(uint32_t) override {}
    void OnSwapchainRecreated() override {}
    void OnRenderPassRecreated() override {}

    void SetMasterVolume(float v) { masterVolume_ = v; audioEngine_.SetMasterVolume(v); }
    [[nodiscard]] float MasterVolume() const noexcept { return masterVolume_; }
    [[nodiscard]] Audio::AudioEngine& Engine() noexcept { return audioEngine_; }
    [[nodiscard]] Audio::Sound& BGM() noexcept { return bgm_; }

    [[nodiscard]] const char* Name() const noexcept override { return "AudioSystem"; }
    [[nodiscard]] int Priority() const noexcept override { return -5; }

private:
    Audio::AudioEngine audioEngine_;
    Audio::Sound bgm_;
    float masterVolume_ = 0.5f;
};

} // namespace BigHero::App