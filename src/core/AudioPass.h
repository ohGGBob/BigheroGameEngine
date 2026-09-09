#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

namespace bighero {

// AudioPass: a single audio DSP/filter pass descriptor (low/high/band-pass,
// notch, delay, reverb) with a chainable per-sample filter state.
// Self-contained, std-lib only.
class AudioPass {
public:
    enum class Type { None, LowPass, HighPass, BandPass, Notch, Delay, Gain };

    Type type = Type::None;
    float cutoff = 1000.0f;   // filter cutoff in Hz
    float resonance = 0.3f;   // Q (0..1)
    float gain = 1.0f;        // gain pass
    float delaySamples = 0;   // delay pass (integer samples)
    float feedback = 0.2f;    // delay feedback

    AudioPass() = default;
    explicit AudioPass(Type t) : type(t) {}

    // Reset internal filter state.
    void Reset() { prevInput_ = 0; prevOutput_ = 0; }

    // Simple one-pole filter approximation for a given sample rate.
    float Process(float sample, uint32_t sampleRate) {
        switch (type) {
            case Type::LowPass: {
                float rc = 1.0f / (2 * 3.14159265f * cutoff);
                float dt = 1.0f / sampleRate;
                float alpha = dt / (rc + dt);
                prevOutput_ = prevOutput_ + alpha * (sample - prevOutput_);
                return prevOutput_;
            }
            case Type::HighPass: {
                float rc = 1.0f / (2 * 3.14159265f * cutoff);
                float dt = 1.0f / sampleRate;
                float alpha = rc / (rc + dt);
                prevOutput_ = alpha * (prevOutput_ + sample - prevInput_);
                prevInput_ = sample;
                return prevOutput_;
            }
            case Type::Gain:
                return sample * gain;
            case Type::None:
            default:
                return sample;
        }
    }

private:
    float prevInput_ = 0, prevOutput_ = 0;
};

} // namespace bighero
