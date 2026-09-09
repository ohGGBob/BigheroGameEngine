#pragma once
#include "FloatCurve_v2.h"
#include "CurveKey_v2.h"

namespace bighero {

// ColorCurve: an RGBA animation curve built from four FloatCurve channels,
// with evaluation, lerp, and hue/clamp helpers. Self-contained.
class ColorCurve {
public:
    void AddKey(float time, float r, float g, float b, float a = 1.0f) {
        r_.AddKey(CurveKey(time, r));
        g_.AddKey(CurveKey(time, g));
        b_.AddKey(CurveKey(time, b));
        a_.AddKey(CurveKey(time, a));
    }
    void Clear() { r_.Clear(); g_.Clear(); b_.Clear(); a_.Clear(); }
    size_t KeyCount() const { return r_.KeyCount(); }
    bool IsEmpty() const { return r_.KeyCount() == 0; }

    // Evaluate the color at time t, writing RGBA (clamped to [0,1]).
    void Evaluate(float t, float& r, float& g, float& b, float& a) const {
        r = Clamp01(r_.Evaluate(t));
        g = Clamp01(g_.Evaluate(t));
        b = Clamp01(b_.Evaluate(t));
        a = Clamp01(a_.Evaluate(t));
    }
    // Component-wise lerp between two RGBA colors.
    static void Lerp(float ar, float ag, float ab, float aa,
                     float br, float bg, float bb, float ba, float t,
                     float& r, float& g, float& b, float& a) {
        r = Clamp01(ar + (br - ar) * t);
        g = Clamp01(ag + (bg - ag) * t);
        b = Clamp01(ab + (bb - ab) * t);
        a = Clamp01(aa + (ba - aa) * t);
    }

private:
    static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    FloatCurve r_, g_, b_, a_;
};

} // namespace bighero
