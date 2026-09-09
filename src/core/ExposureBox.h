#pragma once
#include <cmath>

namespace bighero {

// ExposureBox: an auto-exposure descriptor (histogram/center-weighted) with
// min/max EV clamp, key value, and an exposure-compensation helper.
struct ExposureBox {
    // Min/max average luminance (in EV) to clamp against.
    float minEV = -4.0f;
    float maxEV = 4.0f;
    // Target key value (middle-grey) used to compute exposure.
    float keyValue = 0.18f;
    // Manual exposure compensation in stops (additive EV offset).
    float compensationEV = 0.0f;
    // Adaptation speed (0 = instant, higher = slower adaptation).
    float adaptSpeed = 1.0f;
    bool autoExposure = true;

    ExposureBox() = default;
    explicit ExposureBox(float compensationEV_) : compensationEV(compensationEV_) {}

    // Convert a scene average luminance to a grey-scaled exposure value.
    static float ComputeExposure(float avgLuminance, float keyValue, float compensation) {
        if (avgLuminance <= 1e-6f) return 1.0f;
        float ev = std::log2(avgLuminance > 0 ? avgLuminance : 1e-6f);
        float targetEv = std::log2(keyValue);
        float exposure = targetEv - ev + compensation;
        return std::pow(2.0f, exposure);
    }
    // Clamp an EV value into the [minEV, maxEV] range.
    float ClampEV(float ev) const {
        if (ev < minEV) return minEV;
        if (ev > maxEV) return maxEV;
        return ev;
    }
};

} // namespace bighero
