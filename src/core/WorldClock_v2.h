#pragma once
#include <cstdint>
#include <chrono>

namespace bighero {

// WorldClock: a game-time clock that accumulates scaled delta time. Supports
// time scale (slow-mo / pause) and frame-time smoothing.
// Self-contained, std-lib only.
class WorldClock {
public:
    WorldClock() = default;

    // Begin a new frame; dt is real seconds since the previous frame.
    void Tick(double realDt) {
        realDt_ = realDt > 0 ? realDt : 0;
        double scaled = realDt_ * timeScale_;
        deltaTime_ = scaled;
        smoothedDt_ = smoothedDt_ * 0.9 + scaled * 0.1;
        elapsed_ += scaled;
        ++frameCount_;
    }

    double TimeScale() const { return timeScale_; }
    void SetTimeScale(double s) { timeScale_ = s < 0 ? 0 : s; }

    // Real (unscaled) seconds since last frame.
    double RealDeltaTime() const { return realDt_; }
    // Scaled (game) seconds since last frame.
    double DeltaTime() const { return deltaTime_; }
    double SmoothDeltaTime() const { return smoothedDt_; }
    // Total scaled game time elapsed.
    double Elapsed() const { return elapsed_; }
    uint64_t FrameCount() const { return frameCount_; }

    static double RealNow() {
        using namespace std::chrono;
        return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
    }

    void Reset(double scale = 1.0) {
        timeScale_ = scale;
        elapsed_ = 0; frameCount_ = 0;
        realDt_ = deltaTime_ = smoothedDt_ = 0;
    }

private:
    double timeScale_ = 1.0;
    double realDt_ = 0, deltaTime_ = 0, smoothedDt_ = 0;
    double elapsed_ = 0;
    uint64_t frameCount_ = 0;
};

} // namespace bighero
