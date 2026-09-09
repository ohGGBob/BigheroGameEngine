#pragma once
#include <cmath>

namespace bighero {

// Bloom: a CPU-side bloom effect descriptor with threshold, soft-knee, and
// intensity, plus a reusable luminance/mip-downsample math helper.
struct Bloom {
    // Threshold above which pixels contribute to bloom (in HDR luminance).
    float threshold = 0.8f;
    // Soft-knee transition width around the threshold.
    float softKnee = 0.5f;
    float intensity = 0.8f;
    float radius = 0.6f;   // 0 = tight, 1 = wide scatter
    // Number of downsample/upsample iterations (mip pyramid height).
    int iterations = 5;
    bool enabled = true;

    Bloom() = default;
    explicit Bloom(float threshold_, float intensity_ = 0.8f, bool enabled_ = true)
        : threshold(threshold_), intensity(intensity_), enabled(enabled_) {}

    // Standard bloom pre-filter: returns the bloom contribution for a pixel
    // with linear RGB luminance. Uses a soft-knee curve around the threshold.
    static float PreFilter(float luminance, float threshold_, float softKnee_) {
        float contrib = 0;
        float knee = threshold_ * softKnee_;
        if (luminance <= threshold_ - knee) return 0;
        if (luminance >= threshold_ + knee) contrib = luminance - threshold_;
        else {
            float d = luminance - (threshold_ - knee);
            contrib = d * d / (4 * knee);
        }
        return contrib;
    }
};

} // namespace bighero
