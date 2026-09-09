#pragma once
#include <cmath>

namespace bighero {

// AudioChorus: a chorus effect descriptor (rate/depth/delay) with LFO phase
// and modulation helpers. Self-contained, std-lib only.
class AudioChorus {
public:
    float rate = 0.5f;        // LFO rate in Hz
    float depth = 0.25f;      // modulation depth 0..1
    float delayMs = 20.0f;    // base delay in milliseconds
    float wet = 0.5f;         // wet mix 0..1
    bool enabled = true;

    AudioChorus() = default;
    AudioChorus(float rate_, float depth_, float delayMs_)
        : rate(rate_), depth(depth_), delayMs(delayMs_) {}

    // LFO phase at a given time (0..1).
    float LfoPhase(float timeSec) const {
        float p = rate * timeSec;
        p -= (float)(long)p;
        if (p < 0) p += 1;
        return p;
    }
    // Modulated delay offset in seconds for a given time.
    float ModulatedDelay(float timeSec) const {
        float lfo = std::sin(6.2831853f * rate * timeSec);   // -1..1
        return delayMs * 0.001f + lfo * depth * delayMs * 0.001f;
    }
    float Mix(float input, float chorusOut) const {
        return input * (1.0f - wet) + chorusOut * wet;
    }
};

} // namespace bighero
