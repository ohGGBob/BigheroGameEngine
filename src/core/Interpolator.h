#pragma once
#include <cmath>

namespace bighero {

// Interpolator: generic scalar interpolation strategies selectable at runtime,
// covering the common blend/ease scenarios for animation and procedural work.
class Interpolator {
public:
    enum class Mode { Linear, Smoothstep, Smootherstep, Cosine, Cubic };

    Interpolator() = default;
    explicit Interpolator(Mode mode) : mode_(mode) {}

    void SetMode(Mode m) { mode_ = m; }
    Mode GetMode() const { return mode_; }

    // Interpolate a->b with normalized t in [0,1].
    float Blend(float a, float b, float t) const {
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return a + (b - a) * Apply(t);
    }
    // Eased value of t in [0,1].
    float Apply(float t) const {
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        switch (mode_) {
            case Mode::Linear:      return t;
            case Mode::Smoothstep:  return t * t * (3 - 2 * t);
            case Mode::Smootherstep:return t * t * t * (t * (t * 6 - 15) + 10);
            case Mode::Cosine:      return 0.5f - 0.5f * std::cos(t * 3.14159265f);
            case Mode::Cubic:       return t * t * (2 - t);
            default:                return t;
        }
    }

private:
    Mode mode_ = Mode::Linear;
};

} // namespace bighero
