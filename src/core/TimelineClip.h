#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// TimelineClip: a single clip slot in a timeline with in/out points, blend
// weight, speed and loop settings. Standard-library only, self-contained.
class TimelineClip {
public:
    TimelineClip() {}
    TimelineClip(float start, float duration) { Set(start, duration); }

    void Set(float start, float duration) {
        start_ = start < 0 ? 0 : start;
        duration_ = duration < 0 ? 0 : duration;
    }
    float Start() const { return start_; }
    float Duration() const { return duration_; }
    float End() const { return start_ + duration_; }

    void SetInOut(float in, float out) {
        in_ = in < 0 ? 0 : in; out_ = out > in_ ? out : in_;
    }
    float In() const { return in_; }
    float Out() const { return out_; }

    void SetWeight(float w) { weight_ = w < 0 ? 0 : (w > 1 ? 1 : w); }
    float Weight() const { return weight_; }
    void SetSpeed(float s) { speed_ = s; }
    float Speed() const { return speed_; }
    void SetLoop(bool l) { loop_ = l; }
    bool Loop() const { return loop_; }

    // Map a global time to a clip-local time (accounting for loop).
    float LocalTime(float globalTime) const {
        float t = globalTime - start_;
        if (t < 0) return 0;
        if (duration_ <= 0) return 0;
        float local = t * speed_;
        if (loop_) {
            local -= std::floor(local / duration_) * duration_;
            if (local < 0) local += duration_;
        } else if (local > duration_) {
            local = duration_;
        }
        return local;
    }

private:
    float start_=0, duration_=1;
    float in_=0, out_=1;
    float weight_=1.0f, speed_=1.0f;
    bool loop_=false;
};

} // namespace bighero
