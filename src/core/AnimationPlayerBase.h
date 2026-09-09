#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// AnimationPlayerBase: base interface/time-tracker for playback of an
// animation clip. Maintains playhead, speed, loop mode and normalized time.
// Standard-library only, self-contained.
class AnimationPlayerBase {
public:
    enum class Loop { Clamp, Repeat, PingPong };
    enum class State { Stopped, Playing, Paused };

    AnimationPlayerBase() {}

    void SetDuration(float duration) { duration_ = duration > 0 ? duration : 0.0001f; }
    float Duration() const { return duration_; }

    void SetSpeed(float speed) { speed_ = speed; }
    float Speed() const { return speed_; }

    void SetLoop(Loop l) { loop_ = l; }
    Loop LoopMode() const { return loop_; }

    State GetState() const { return state_; }

    void Play() { state_ = State::Playing; }
    void Pause() { if (state_ == State::Playing) state_ = State::Paused; }
    void Stop() { state_ = State::Stopped; time_ = 0; }

    void SetTime(float t) { time_ = t; }
    float Time() const { return time_; }

    // Advance time by dt (respects speed + loop). Returns the wrapped time.
    float Advance(float dt) {
        if (state_ != State::Playing) return time_;
        float t = time_ + dt * speed_;
        t = Wrap(t);
        time_ = t;
        return time_;
    }

    // Normalized time in [0,1].
    float NormalizedTime() const { return (time_ / duration_) ; }

    bool IsFinished() const {
        if (loop_ == Loop::Repeat || loop_ == Loop::PingPong) return false;
        return time_ >= duration_;
    }

protected:
    float Wrap(float t) const {
        switch (loop_) {
            case Loop::Repeat: {
                float f = t - std::floor(t / duration_) * duration_;
                return f >= 0 ? f : f + duration_;
            }
            case Loop::PingPong: {
                float f = t - std::floor(t / duration_) * duration_;
                bool reversed = ((int)std::floor(t / duration_) & 1) != 0;
                return reversed ? (duration_ - f) : f;
            }
            case Loop::Clamp:
            default:
                if (t < 0) return 0;
                if (t > duration_) return duration_;
                return t;
        }
    }

    float duration_ = 1.0f;
    float speed_ = 1.0f;
    float time_ = 0.0f;
    Loop loop_ = Loop::Clamp;
    State state_ = State::Stopped;
};

} // namespace bighero
