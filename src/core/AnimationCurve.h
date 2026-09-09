#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// AnimationCurve: a piecewise keyframe curve with configurable interpolation.
// Holds a sorted list of (time, value) keys and evaluates the value at any
// time with linear or smoothstep interpolation. Pure stdlib.
class AnimationCurve {
public:
    enum class Interp { Linear, SmoothStep };

    struct Key { float time; float value; };

    AnimationCurve() {}

    void AddKey(float time, float value) {
        // Insert keeping sorted order by time.
        std::size_t pos = 0;
        while (pos < keys_.size() && keys_[pos].time < time) ++pos;
        keys_.insert(keys_.begin() + pos, {time, value});
    }
    void Clear() { keys_.clear(); }
    std::size_t KeyCount() const { return keys_.size(); }
    bool GetKey(std::size_t i, float& time, float& value) const {
        if (i >= keys_.size()) return false;
        time = keys_[i].time; value = keys_[i].value;
        return true;
    }

    void SetInterp(Interp m) { interp_ = m; }
    Interp Interpolation() const { return interp_; }
    void SetLoop(bool l) { loop_ = l; }
    bool Loop() const { return loop_; }

    float Evaluate(float t) const {
        if (keys_.empty()) return 0.0f;
        if (keys_.size() == 1) return keys_[0].value;
        if (t <= keys_.front().time) return keys_.front().value;
        if (t >= keys_.back().time) {
            if (!loop_) return keys_.back().value;
            // wrap into the curve range for looping
            float span = keys_.back().time - keys_.front().time;
            if (span <= 0) return keys_.back().value;
            t = keys_.front().time + std::fmod(t - keys_.front().time, span);
        }
        // find surrounding keys
        for (std::size_t i = 1; i < keys_.size(); ++i) {
            if (t <= keys_[i].time) {
                const Key& a = keys_[i - 1];
                const Key& b = keys_[i];
                float dt = b.time - a.time;
                if (dt <= 0) return b.value;
                float u = (t - a.time) / dt;
                if (interp_ == Interp::SmoothStep) u = u * u * (3 - 2 * u);
                return a.value + (b.value - a.value) * u;
            }
        }
        return keys_.back().value;
    }

    float StartTime() const { return keys_.empty() ? 0 : keys_.front().time; }
    float EndTime() const { return keys_.empty() ? 0 : keys_.back().time; }

private:
    std::vector<Key> keys_;
    Interp interp_ = Interp::Linear;
    bool loop_ = false;
};

} // namespace bighero
