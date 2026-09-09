#pragma once
#include <cstddef>

namespace bighero {

// AudioFilter: settings for a per-channel audio filter (low-pass/high-pass/
// band-pass). Holds type, cutoff, and Q/resonance. Pure config container the
// audio backend applies to a voice/bus.
class AudioFilter {
public:
    enum class Type { None, LowPass, HighPass, BandPass, Notch };

    AudioFilter() {}
    explicit AudioFilter(Type t) : type_(t) {}

    void SetType(Type t) { type_ = t; }
    Type Current() const { return type_; }
    void SetCutoff(float hz) { cutoff_ = hz < 20 ? 20 : (hz > 20000 ? 20000 : hz); }
    float Cutoff() const { return cutoff_; }
    void SetResonance(float q) { q_ = q < 0.01f ? 0.01f : (q > 20 ? 20 : q); }
    float Resonance() const { return q_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetMix(float m) { mix_ = m < 0 ? 0 : (m > 1 ? 1 : m); }
    float Mix() const { return mix_; }

    // Simple one-pole low-pass coefficient (0..1) — higher = brighter.
    float OnePoleCoef(float sampleRate) const {
        if (sampleRate < 1) sampleRate = 1;
        float dt = 1.0f / sampleRate;
        float rc = 1.0f / (2.0f * 3.14159265f * cutoff_);
        float alpha = dt / (rc + dt);
        return alpha;
    }
    void Reset() { type_ = Type::None; cutoff_ = 20000.0f; q_ = 0.707f; mix_ = 1.0f; enabled_ = false; }

private:
    Type type_ = Type::None;
    float cutoff_ = 20000.0f, q_ = 0.707f, mix_ = 1.0f;
    bool enabled_ = false;
};

} // namespace bighero
