#pragma once
#include <cstdint>
#include <functional>

namespace bighero {

// Timer: a simple one-shot or repeating timer driven by Tick(dt). Self-contained,
// std-lib only.
class Timer {
public:
    enum class Mode : int { OneShot = 0, Repeat = 1 };

    Timer() = default;
    Timer(float duration, Mode mode, std::function<void()> callback)
        : duration_(duration), mode_(mode), callback_(std::move(callback)) {}

    void Start() { running_ = true; elapsed_ = 0; }
    void Stop() { running_ = false; elapsed_ = 0; }
    void Reset() { elapsed_ = 0; }
    void Pause() { paused_ = true; }
    void Resume() { paused_ = false; }

    void SetDuration(float d) { duration_ = d > 0 ? d : 0; }
    float Duration() const { return duration_; }
    void SetCallback(std::function<void()> cb) { callback_ = std::move(cb); }

    bool IsRunning() const { return running_ && !paused_; }
    float Progress() const { return duration_ > 0 ? elapsed_ / duration_ : 0; }
    float Remaining() const { return duration_ - elapsed_; }

    // Advance by dt seconds; invoke callback once per full duration crossed.
    bool Tick(float dt) {
        if (!IsRunning()) return false;
        if (duration_ <= 0) return false;
        elapsed_ += dt;
        bool fired = false;
        while (elapsed_ >= duration_) {
            if (callback_) callback_();
            fired = true;
            if (mode_ == Mode::OneShot) { running_ = false; break; }
            elapsed_ -= duration_;
        }
        return fired;
    }

private:
    float duration_ = 1.0f;
    float elapsed_ = 0;
    Mode mode_ = Mode::OneShot;
    std::function<void()> callback_;
    bool running_ = false;
    bool paused_ = false;
};

} // namespace bighero
