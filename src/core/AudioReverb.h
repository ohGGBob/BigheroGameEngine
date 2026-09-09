#pragma once
#include <cmath>

namespace bighero {

// AudioReverb: a simple reverb/echo descriptor with delay, feedback, and
// wet/dry mixing helpers. Self-contained, std-lib only.
class AudioReverb {
public:
    float delaySeconds = 0.3f;   // echo delay
    float feedback = 0.4f;       // 0..1 feedback gain
    float wet = 0.3f;            // wet mix 0..1
    float dry = 0.7f;            // dry mix 0..1
    bool enabled = true;
    int roomSize = 5;            // 0..10 reverberation room size hint

    AudioReverb() = default;
    explicit AudioReverb(float delay, float feedback_ = 0.4f, float wet_ = 0.3f)
        : delaySeconds(delay), feedback(feedback_), wet(wet_) {}

    // Evaluate the feedback coefficient clamped to [0,0.95] (stability).
    float StableFeedback() const {
        return feedback < 0 ? 0 : (feedback > 0.95f ? 0.95f : feedback);
    }
    // Sample count for a given sample rate (rounded).
    size_t DelaySamples(uint32_t sampleRate) const {
        return (size_t)(delaySeconds * (float)sampleRate);
    }
    // Wet/dry blend factor.
    float Mix(float input, float echoed) const {
        return input * dry + echoed * wet;
    }
};

} // namespace bighero
