#pragma once
#include <cmath>

namespace bighero {

// CurveKey: a single keyframe point on an animation curve with ease-in/out
// tangents and interpolation mode, used by AnimationCurve. Self-contained.
class CurveKey {
public:
    enum class Mode { Linear, Constant, Smooth, Bezier };

    float time = 0;
    float value = 0;
    // In/out tangents for Bezier mode.
    float inTangent = 0;
    float outTangent = 0;
    Mode mode = Mode::Smooth;

    CurveKey() = default;
    CurveKey(float time_, float value_, Mode mode_ = Mode::Smooth)
        : time(time_), value(value_), mode(mode_) {}

    void SetTangents(float in, float out) { inTangent = in; outTangent = out; }

    // Evaluate this key's local contribution for a normalized local t in [0,1]
    // toward the next key. nextValue is the next keyframe's value.
    float Evaluate(float nextValue, float localT) const {
        if (localT < 0) localT = 0;
        if (localT > 1) localT = 1;
        if (mode == Mode::Linear)
            return value + (nextValue - value) * localT;
        if (mode == Mode::Constant)
            return value;  // hold until the next key
        if (mode == Mode::Bezier) {
            float t2 = localT * localT, t3 = t2 * localT;
            float h00 = 2*t3 - 3*t2 + 1;
            float h10 = t3 - 2*t2 + localT;
            float h01 = -2*t3 + 3*t2;
            float h11 = t3 - t2;
            return h00*value + h10*outTangent + h01*nextValue + h11*inTangent;
        }
        // Smooth (Hermite with default tangents -> smoothstep-like).
        float t = localT;
        return value + (nextValue - value) * (t * t * (3 - 2 * t));
    }
};

} // namespace bighero
