#pragma once
#include <cstdint>
#include <cstddef>

namespace bighero {

// TimeSystem: a game clock that tracks frame deltas with an adjustable
// time scale and optional pause. Provides scaled delta, unscaled delta, and
// accumulated total time. Pure stdlib.
class TimeSystem {
public:
    TimeSystem() {}

    void SetTimeScale(float s) { timeScale_ = s < 0 ? 0 : s; }
    float TimeScale() const { return timeScale_; }
    void SetPaused(bool p) { paused_ = p; }
    bool Paused() const { return paused_; }
    void TogglePause() { paused_ = !paused_; }

    // Advance the clock by a (already frame-rate-scaled) raw delta.
    void Advance(float rawDelta) {
        unscaledDelta_ = rawDelta;
        scalar_ = paused_ ? 0.0f : timeScale_;
        delta_ = rawDelta * scalar_;
        time_ += delta_;
        ++frame_;
        if (frame_ == 0) frame_ = 1; // avoid wrap
    }

    float Delta() const { return delta_; }
    float UnscaledDelta() const { return unscaledDelta_; }
    float Time() const { return time_; }
    std::uint64_t Frame() const { return frame_; }

    void Reset() { time_ = 0; frame_ = 0; delta_ = 0; unscaledDelta_ = 0; paused_ = false; timeScale_ = 1.0f; }

private:
    float timeScale_ = 1.0f;
    bool paused_ = false;
    float delta_ = 0;
    float unscaledDelta_ = 0;
    float time_ = 0;
    std::uint64_t frame_ = 0;
    float scalar_ = 1.0f;
};

} // namespace bighero
