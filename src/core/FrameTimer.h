#pragma once
#include <cstdint>

namespace bighero {

// Frame timer tracking deltas and timers that tick by accumulated time.
class FrameTimer {
public:
    FrameTimer() : frameTime_(0), lastTime_(0), totalTime_(0), frameCount_(0) {}

    // Call once per frame with current time (seconds).
    void Tick(float currentTime) {
        if (lastTime_ == 0) frameTime_ = 0;
        else frameTime_ = currentTime - lastTime_;
        lastTime_ = currentTime;
        totalTime_ = currentTime;
        ++frameCount_;
    }

    float DeltaTime() const { return frameTime_; }
    float TotalTime() const { return totalTime_; }
    std::uint64_t FrameCount() const { return frameCount_; }
    float Fps() const { return frameTime_ > 0 ? 1.0f / frameTime_ : 0.0f; }

    // Simple countdown timer (seconds). Returns true while running.
    bool TickTimer(float& timer, float dt) {
        if (timer > 0) { timer -= dt; if (timer <= 0) { timer = 0; return false; } return true; }
        return false;
    }

private:
    float frameTime_, lastTime_, totalTime_;
    std::uint64_t frameCount_;
};

} // namespace bighero
