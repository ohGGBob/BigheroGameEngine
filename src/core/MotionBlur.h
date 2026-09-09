#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// MotionBlur: a post-process that accumulates intensity of motion. Tracks
// a per-object velocity magnitude contribution and blends it by a strength
// factor and sample count. Pure data/config container.
class MotionBlur {
public:
    MotionBlur() {}

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetStrength(float s) { strength_ = s < 0 ? 0 : s; }
    float Strength() const { return strength_; }
    void SetSamples(int n) { samples_ = n < 1 ? 1 : n; }
    int Samples() const { return samples_; }
    // Blend (or blur) amount per exposed property.
    void SetBlend(float b) { blend_ = b < 0 ? 0 : (b > 1 ? 1 : b); }
    float Blend() const { return blend_; }

    void Reset() { enabled_ = false; strength_ = 0.5f; samples_ = 8; blend_ = 0.5f; }

    // Record a velocity magnitude for an object; returns current blend level.
    float AddVelocity(float v) {
        if (!enabled_) return 0.0f;
        float scaled = v * strength_;
        running_ += scaled;
        return blend_ * scaled;
    }
    float Accumulated() const { return running_; }
    void ClearAccumulation() { running_ = 0; }

private:
    bool enabled_ = false;
    float strength_ = 0.5f;
    int samples_ = 8;
    float blend_ = 0.5f;
    float running_ = 0;
};

} // namespace bighero
