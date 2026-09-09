#pragma once
#include <cmath>

namespace bighero {

// Skybox: describes a skybox/atmosphere state (gradient or cubemap based)
// with sun direction, exposure, and fog blending. Self-contained descriptor.
struct Skybox {
    // Gradient sky colors (top/mid/bottom in RGB).
    float topR = 0.25f, topG = 0.45f, topB = 0.9f;
    float midR = 0.6f,  midG = 0.75f, midB = 0.95f;
    float botR = 0.9f,  botG = 0.9f,  botB = 0.9f;
    // Sun/bright spot direction (normalized by caller) and intensity.
    float sunX = 0.3f, sunY = 0.8f, sunZ = 0.4f;
    float sunIntensity = 1.0f;
    float sunColorR = 1.0f, sunColorG = 0.95f, sunColorB = 0.85f;
    // Exposure / brightness multiplier.
    float exposure = 1.0f;
    bool enableFog = true;

    Skybox() = default;

    // Sample the vertical gradient at normalized height t in [0,1].
    void SampleGradient(float t, float& r, float& g, float& b) const {
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        if (t < 0.5f) {
            float f = t * 2;
            r = Lerp(midR, topR, f); g = Lerp(midG, topG, f); b = Lerp(midB, topB, f);
        } else {
            float f = (t - 0.5f) * 2;
            r = Lerp(botR, midR, f); g = Lerp(botG, midG, f); b = Lerp(botB, midB, f);
        }
        r = Clamp01(r * exposure); g = Clamp01(g * exposure); b = Clamp01(b * exposure);
    }

    static float Lerp(float a, float b, float f) { return a + (b - a) * f; }
    static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
};

} // namespace bighero
