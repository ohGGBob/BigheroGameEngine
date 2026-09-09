#pragma once
#include <cmath>

namespace bighero {

// AudioDistortion: a distortion/overdrive effect descriptor with drive,
// tone, and clipping helpers. Self-contained, std-lib only.
class AudioDistortion {
public:
    float drive = 1.0f;       // pre-gain
    float tone = 0.5f;        // low-pass tone 0..1
    float mix = 1.0f;         // dry/wet mix 0..1
    bool enabled = true;
    // Soft-clip versus hard-clip.
    bool softClip = true;

    AudioDistortion() = default;
    explicit AudioDistortion(float drive_, float mix_ = 1.0f, bool soft_ = true)
        : drive(drive_), mix(mix_), softClip(soft_) {}

    // Soft-clip a sample (tanh style), driving it through a gain.
    float Distort(float sample) const {
        float g = sample * drive;
        if (softClip) {
            // Soft clipping approximating tanh(g) mapped to [-1,1].
            float s = std::tanh(g);
            return s;
        }
        // Hard clip to [-1,1].
        if (g > 1.0f) return 1.0f;
        if (g < -1.0f) return -1.0f;
        return g;
    }
    // Wet/dry mix.
    float Mix(float dry, float wetSample) const {
        return dry * (1.0f - mix) + wetSample * mix;
    }
};

} // namespace bighero
