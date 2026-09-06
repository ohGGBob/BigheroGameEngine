#pragma once
// 时间管理：帧计时、FPS 统计、时间累积器
// 单一职责：原 Application 中 lastTime_/fpsTimer_/deltaTime_/FPS 统计逻辑

#include "ISubSystem.h"
#include <chrono>
#include <string>
#include <array>

namespace BigHero::App
{

class TimeManager final : public ISubSystem
{
public:
    explicit TimeManager(const std::string& windowTitle = "BigHero Engine")
        : baseTitle_(windowTitle), lastTime_(GetTimeSeconds()) {}

    [[nodiscard]] const char* Name() const noexcept override { return "TimeManager"; }
    [[nodiscard]] int Priority() const noexcept override { return -100; } // 最早更新

    void Init() override {}
    void Shutdown() override {}

    void Update(const FrameContext& frame) override
    {
        const double currentTime = GetTimeSeconds();
        deltaTime_ = static_cast<float>(currentTime - lastTime_);
        lastTime_ = currentTime;

        // FPS 统计（0.5 秒窗口）
        fpsTimer_ += deltaTime_;
        ++fpsFrames_;
        if (fpsTimer_ >= 0.5)
        {
            lastFps_ = static_cast<uint32_t>(std::lround(fpsFrames_ / fpsTimer_));
            fpsTimer_ = 0.0;
            fpsFrames_ = 0;
        }

        // 历史帧时间（用于图表显示）
        static size_t historyIdx = 0;
        frameTimeHistory_[historyIdx] = deltaTime_ * 1000.0f;  // ms
        historyIdx = (historyIdx + 1) % kHistorySize;
    }

    [[nodiscard]] float DeltaTime() const noexcept { return deltaTime_; }
    [[nodiscard]] uint32_t LastFps() const noexcept { return lastFps_; }
    [[nodiscard]] const std::array<float, kHistorySize>& FrameTimeHistory() const noexcept { return frameTimeHistory_; }

    void SetRendererSampleCount(uint32_t count) noexcept { rendererSampleCount_ = count; }

private:
    static constexpr size_t kHistorySize = 120;
    std::array<float, kHistorySize> frameTimeHistory_{};
    double lastTime_ = 0.0;
    double fpsTimer_ = 0.0;
    uint32_t fpsFrames_ = 0;
    uint32_t lastFps_ = 0;
    float deltaTime_ = 0.0f;
    std::string baseTitle_;
    uint32_t rendererSampleCount_ = 4;

    static double GetTimeSeconds() noexcept
    {
        return glfwGetTime();
    }
};

} // namespace BigHero::App