#pragma once
#include <cmath>

namespace bighero {

// CameraShake: procedural camera offset based on decaying noise/random
// amplitude. Provides an offset (x,y) and rotational roll to apply each
// frame to a camera transform. Pure stdlib.
class CameraShake {
public:
    CameraShake() {}

    void Trigger(float amplitude, float duration, float frequency = 20.0f) {
        amplitude_ = amplitude < 0 ? 0 : amplitude;
        duration_ = duration < 0 ? 0 : duration;
        frequency_ = frequency <= 0 ? 1 : frequency;
        time_ = 0;
        active_ = true;
    }
    void Stop() { active_ = false; amplitude_ = 0; }
    bool IsActive() const { return active_; }

    // Advance by dt; returns true if still shaking after update.
    bool Update(float dt, float /*seed*/) {
        if (!active_) return false;
        time_ += dt;
        if (time_ >= duration_) { active_ = false; amplitude_ = 0; return false; }
        phase_ += (frequency_ * dt);
        return true;
    }

    // Current positional offset scaled by decay.
    void Offset(float& ox, float& oy) const {
        float decay = active_ ? (1.0f - (time_ / duration_)) : 0.0f;
        decay = decay < 0 ? 0 : decay;
        ox = cosf(phase_ * 1.7f) * amplitude_ * decay;
        oy = sinf(phase_ * 2.3f) * amplitude_ * decay;
    }
    float Roll() const {
        float decay = active_ ? (1.0f - (time_ / duration_)) : 0.0f;
        decay = decay < 0 ? 0 : decay;
        return sinf(phase_ * 3.1f) * 0.15f * amplitude_ * decay;
    }

    float Amplitude() const { return amplitude_; }
    float Duration() const { return duration_; }
    float Remaining() const { return active_ ? (duration_ - time_) : 0.0f; }

private:
    float amplitude_ = 0;
    float duration_ = 0;
    float frequency_ = 20.0f;
    float time_ = 0;
    float phase_ = 0;
    bool active_ = false;
};

} // namespace bighero
