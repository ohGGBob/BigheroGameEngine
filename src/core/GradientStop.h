#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// GradientStop: a single color stop within a gradient timeline (position +
// RGBA color + optional blend mode).
struct GradientStop {
    float position = 0.0f;
    float r = 1, g = 1, b = 1, a = 1;

    GradientStop() = default;
    GradientStop(float pos, float r_, float g_, float b_, float a_ = 1.0f)
        : position(pos), r(r_), g(g_), b(b_), a(a_) {}

    void SetColor(float r_, float g_, float b_, float a_ = 1.0f) {
        r = r_; g = g_; b = b_; a = a_;
    }
    static GradientStop Lerp(const GradientStop& s0, const GradientStop& s1, float t) {
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        GradientStop out;
        out.position = s0.position + (s1.position - s0.position) * t;
        out.r = s0.r + (s1.r - s0.r) * t;
        out.g = s0.g + (s1.g - s0.g) * t;
        out.b = s0.b + (s1.b - s0.b) * t;
        out.a = s0.a + (s1.a - s0.a) * t;
        return out;
    }
};

} // namespace bighero
