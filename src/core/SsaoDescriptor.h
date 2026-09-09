#pragma once
#include <cstdint>
#include <cstddef>

namespace bighero {

// SsaoDescriptor: screen-space ambient occlusion settings. Holds the radius,
// bias, sample count, intensity, and optional blur pass parameters. Pure
// config container the SSAO post-process reads.
class SsaoDescriptor {
public:
    SsaoDescriptor() {}
    explicit SsaoDescriptor(int sampleCount)
        : samples_(sampleCount < 1 ? 1 : sampleCount) {}

    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }
    void SetBias(float b) { bias_ = b; }
    float Bias() const { return bias_; }
    void SetIntensity(float i) { intensity_ = i < 0 ? 0 : i; }
    float Intensity() const { return intensity_; }
    void SetSampleCount(int n) { samples_ = n < 1 ? 1 : n; }
    int SampleCount() const { return samples_; }

    void SetBlur(bool b) { blur_ = b; }
    bool BlurEnabled() const { return blur_; }
    void SetBlurRadius(int r) { blurRadius_ = r < 0 ? 0 : r; }
    int BlurRadius() const { return blurRadius_; }

    void SetNormalScale(float s) { normalScale_ = s; }
    float NormalScale() const { return normalScale_; }
    void SetPowerExponent(float p) { power_ = p < 1 ? 1 : p; }
    float PowerExponent() const { return power_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetDebugDraw(bool d) { debugDraw_ = d; }
    bool DebugDraw() const { return debugDraw_; }

    void Reset() {
        radius_ = 0.5f; bias_ = 0.025f; intensity_ = 1.0f; samples_ = 16;
        blur_ = true; blurRadius_ = 2; normalScale_ = 1.0f; power_ = 2.0f;
        enabled_ = true; debugDraw_ = false;
    }

private:
    float radius_ = 0.5f, bias_ = 0.025f, intensity_ = 1.0f;
    int samples_ = 16;
    bool blur_ = true;
    int blurRadius_ = 2;
    float normalScale_ = 1.0f, power_ = 2.0f;
    bool enabled_ = true, debugDraw_ = false;
};

} // namespace bighero
