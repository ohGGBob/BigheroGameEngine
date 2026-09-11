#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// AnimationCurveFrame: a (time, value) sample within an animation curve.
// Self-contained / std-lib only.
class AnimationCurveFrame {
public:
    AnimationCurveFrame() = default;
    AnimationCurveFrame(float time, float value) : time_(time), value_(value) {}

    void SetTime(float t) { time_ = t; }
    float Time() const { return time_; }
    void SetValue(float v) { value_ = v; }
    float Value() const { return value_; }
    void Set(float t, float v) { time_=t; value_=v; }

    bool IsValid() const { return time_ >= 0.0f; }
    // Linear interpolation from this frame toward another at local t in [0,1].
    static float Lerp(const AnimationCurveFrame& a, const AnimationCurveFrame& b, float t) {
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        return a.value_ + (b.value_ - a.value_) * t;
    }

private:
    float time_ = 0, value_ = 0;
};

} // namespace bighero
