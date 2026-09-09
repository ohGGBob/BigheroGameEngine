#pragma once
#include <cstddef>
#include <cstdint>

namespace bighero {

// ScreenSpaceReflection: settings for an SSR pass — thickness, step count,
// max ray distance, and fade range. Pure config container read by the
// post-process SSR shader.
class ScreenSpaceReflection {
public:
    ScreenSpaceReflection() {}
    explicit ScreenSpaceReflection(int steps) : steps_(steps < 1 ? 1 : steps) {}

    void SetThickness(float t) { thickness_ = t < 0 ? 0 : t; }
    float Thickness() const { return thickness_; }
    void SetStepCount(int n) { steps_ = n < 1 ? 1 : n; }
    int StepCount() const { return steps_; }
    void SetMaxDistance(float d) { maxDistance_ = d < 0 ? 0 : d; }
    float MaxDistance() const { return maxDistance_; }
    void SetFadeRange(float r) { fade_ = r < 0 ? 0 : r; }
    float FadeRange() const { return fade_; }
    void SetIntensity(float i) { intensity_ = i < 0 ? 0 : i; }
    float Intensity() const { return intensity_; }
    void SetAccumulationFactor(float f) { acc_ = f < 0 ? 0 : (f > 1 ? 1 : f); }
    float AccumulationFactor() const { return acc_; }

    void SetHiZ(bool h) { hiz_ = h; }
    bool HiZEnabled() const { return hiz_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    void Reset() {
        thickness_ = 0.1f; steps_ = 32; maxDistance_ = 100.0f;
        fade_ = 20.0f; intensity_ = 1.0f; acc_ = 0.8f;
        hiz_ = true; enabled_ = true;
    }

private:
    float thickness_ = 0.1f;
    int steps_ = 32;
    float maxDistance_ = 100.0f, fade_ = 20.0f, intensity_ = 1.0f, acc_ = 0.8f;
    bool hiz_ = true, enabled_ = true;
};

} // namespace bighero
