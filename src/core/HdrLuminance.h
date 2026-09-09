#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// HdrLuminance: computes scene-adaptive exposure from a histogram/luminance
// average. Holds target and min/max exposure clamps plus helper exposure math.
// Pure CPU-side config + calculation.
class HdrLuminance {
public:
    HdrLuminance() {}
    explicit HdrLuminance(float target) : target_(target) {}

    void SetTarget(float t) { target_ = t < 0.01f ? 0.01f : t; }
    float Target() const { return target_; }
    void SetMinExposure(float m) { minExposure_ = m; }
    float MinExposure() const { return minExposure_; }
    void SetMaxExposure(float m) { maxExposure_ = m; }
    float MaxExposure() const { return maxExposure_; }
    void SetAdaptationSpeed(float s) { speed_ = s < 0 ? 0 : s; }
    float AdaptationSpeed() const { return speed_; }

    // Compute exposure from a scene average luminance (log-avg scale).
    float ComputeExposure(float avgLuminance) const {
        float key = avgLuminance < 0.0001f ? 0.0001f : avgLuminance;
        float e = target_ / key;
        if (e < minExposure_) e = minExposure_;
        if (e > maxExposure_) e = maxExposure_;
        return e;
    }

    // Scalar tone-map operator: HDR value -> [0,1] using exposure + gamma.
    float ToneMap(float hdrValue, float exposure) const {
        float v = hdrValue * exposure;
        v = v / (1.0f + v);   // reinhard-style
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        return v;
    }

    void Reset() { target_ = 0.18f; minExposure_ = 0.0f; maxExposure_ = 8.0f; speed_ = 0.5f; }

private:
    float target_ = 0.18f, minExposure_ = 0.0f, maxExposure_ = 8.0f, speed_ = 0.5f;
};

} // namespace bighero
