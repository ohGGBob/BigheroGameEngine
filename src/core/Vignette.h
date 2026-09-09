#pragma once
#include <cmath>

namespace bighero {

// Vignette: a reusable vignette effect descriptor and factor evaluator.
// Self-contained, pure math.
struct Vignette {
    float strength = 0.5f;
    // Inner radius (no vignette within this normalized radius).
    float radius = 0.9f;
    float softness = 0.7f;
    float colorR = 0, colorG = 0, colorB = 0;
    bool enabled = true;

    Vignette() = default;

    // Vignette factor at normalized coordinates nx,ny in [-1,1].
    // Returns 1 at center (no darkening), 0 at edge (full darkening).
    float Factor(float nx, float ny) const {
        float d = std::sqrt(nx * nx + ny * ny);
        if (d <= radius) return 1.0f;
        float f = (d - radius) / (2.0f - radius > 1e-3f ? (2.0f - radius) : 1.0f);
        f = f < 0 ? 0 : (f > 1 ? 1 : f);
        float v = 1.0f - f * strength;
        return v < 0 ? 0 : v;
    }
};

} // namespace bighero
