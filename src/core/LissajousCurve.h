#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// LissajousCurve: a parametric Lissajous (Bowditch) curve.
//   x = A*sin(a*t + d), y = B*sin(b*t)
// Provides point evaluation over t in [0, 2*pi]. Standard-library only,
// self-contained.
class LissajousCurve {
public:
    LissajousCurve() {}
    LissajousCurve(float ax, float ay, float a, float b, float phase = 0.0f)
        { Set(ax, ay, a, b, phase); }

    void Set(float ax, float ay, float a, float b, float phase = 0.0f) {
        ax_=ax; ay_=ay; a_=a; b_=b; phase_=phase;
    }
    float AmplitudeX() const { return ax_; }
    float AmplitudeY() const { return ay_; }
    float FrequencyX() const { return a_; }
    float FrequencyY() const { return b_; }
    float Phase() const { return phase_; }

    // Evaluate at parameter t in [0, 2*pi].
    void Evaluate(float t, float& x, float& y) const {
        x = ax_ * std::sin(a_*t + phase_);
        y = ay_ * std::sin(b_*t);
    }

    // Evaluate at normalized u in [0,1] mapping to t = u*2*pi.
    void EvaluateNormalized(float u, float& x, float& y) const {
        float t = u * 6.2831853f;
        Evaluate(t, x, y);
    }

private:
    float ax_=1, ay_=1, a_=1, b_=1, phase_=0;
};

} // namespace bighero
