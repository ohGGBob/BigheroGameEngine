#pragma once
#include <cmath>

namespace bighero {

// SSAO: a screen-space ambient-occlusion descriptor with radius, bias,
// intensity, and sample count, plus a reusable hemisphere-disk sample helper.
struct SSAO {
    // Occlusion sample radius in world units.
    float radius = 0.5f;
    // Depth bias to avoid self-occlusign artifacts.
    float bias = 0.025f;
    // Occlusion strength (linear multiplier on the AO factor).
    float intensity = 1.0f;
    // Number of samples (also the kernel size, clamped to the renderer's max).
    int sampleCount = 16;
    bool enabled = true;

    SSAO() = default;
    explicit SSAO(float radius_, int sampleCount_ = 16, float intensity_ = 1.0f)
        : radius(radius_), intensity(intensity_), sampleCount(sampleCount_) {}

    // Simple "sphere/hemisphere" AO falloff for a fragment at depth d given an
    // occlusion sample at depth sd. Returns an AO factor (1 = fully lit).
    static float OcclusionFactor(float d, float sd, float bias_, float radius_) {
        float diff = sd - d;
        if (diff < bias_) return 1.0f;
        float occ = 1.0f - diff / (radius_ * radius_ + 1e-6f);
        return occ < 0 ? 0 : occ;
    }
};

} // namespace bighero
