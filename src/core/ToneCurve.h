#pragma once
#include <cmath>

namespace bighero {

// ToneCurve: an RGB tone-mapping curve descriptor with lift/gamma/gain and a
// reusable tone-map evaluation. Self-contained, std-lib only.
struct ToneCurve {
    // Lift (dark foot lift), gamma (mid-tone exponent), gain (bright clip).
    float lift = 0.0f;
    float gamma = 1.0f;
    float gain = 1.0f;
    // Optional white point (tone-map scaling).
    float whitePoint = 1.0f;

    ToneCurve() = default;

    // Apply the curve to a single linear RGB channel v in [0,1].
    float ApplyChannel(float v) const {
        v = v < 0 ? 0 : (v > 1 ? 1 : v);
        float x = v + lift;
        if (gamma > 0) x = std::pow(x, 1.0f / gamma);
        x *= gain;
        x *= whitePoint;
        return x < 0 ? 0 : (x > 1 ? 1 : x);
    }
    // Apply the curve to a linear RGB triple.
    void Apply(float& r, float& g, float& b) const {
        r = ApplyChannel(r);
        g = ApplyChannel(g);
        b = ApplyChannel(b);
    }
};

} // namespace bighero
