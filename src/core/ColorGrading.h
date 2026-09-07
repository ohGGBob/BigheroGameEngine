#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// Color grading helpers: simple per-channel adjustments for a RGBA8 pipeline.
// All inputs in [0,1]. Pure standard library.
class ColorGrading {
public:
    static float Clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

    // Exposure via scale.
    static float Exposure(float v, float exposure) { return Clamp01(v * exposure); }

    // Contrast (factor around 0.5).
    static float Contrast(float v, float factor) {
        return Clamp01((v - 0.5f) * factor + 0.5f);
    }

    // Saturation (factor; 0 = grayscale, 1 = original, >1 = more).
    static float Saturation(float r, float g, float b, float factor,
                            float& or_, float& og, float& ob) {
        float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        or_ = Clamp01(luma + (r - luma) * factor);
        og = Clamp01(luma + (g - luma) * factor);
        ob = Clamp01(luma + (b - luma) * factor);
        return luma;
    }

    // Simple vignette attenuation based on normalized radial distance.
    static float Vignette(float nx, float ny, float strength) {
        float dx = nx - 0.5f, dy = ny - 0.5f;
        float dist = std::sqrt(dx * dx + dy * dy) * 2.0f; // 0 center, ~1 corner
        return Clamp01(1.0f - strength * dist * dist);
    }

    // Apply exposure + contrast + saturation to a single pixel in place.
    static void Grade(float& r, float& g, float& b, float exposure, float contrast, float sat) {
        r = Exposure(r, exposure); g = Exposure(g, exposure); b = Exposure(b, exposure);
        r = Contrast(r, contrast); g = Contrast(g, contrast); b = Contrast(b, contrast);
        float or_, og, ob; Saturation(r, g, b, sat, or_, og, ob);
        r = or_; g = og; b = ob;
    }
};

} // namespace bighero
