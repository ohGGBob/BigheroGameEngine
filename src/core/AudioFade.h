#pragma once
#include <cmath>

namespace bighero {

// AudioFade: a fade-in/out (and crossfade) envelope descriptor with linear,
// smoothstep, and exponential curves. Self-contained, std-lib only.
class AudioFade {
public:
    enum class Curve { Linear, Smoothstep, Exponential };

    float fadeInTime = 0.0f;   // seconds
    float fadeOutTime = 0.0f;  // seconds
    float holdTime = 0.0f;     // seconds at full volume between fades
    Curve curve = Curve::Linear;
    bool enabled = true;

    AudioFade() = default;
    AudioFade(float in, float out, Curve c = Curve::Linear)
        : fadeInTime(in), fadeOutTime(out), curve(c) {}

    // Envelope gain at a given playhead time t (seconds), in [0,1].
    float Evaluate(float t) const {
        if (!enabled || t < 0) return 0;
        if (fadeInTime > 0 && t < fadeInTime) {
            float f = t / fadeInTime;
            return Apply(f);
        }
        float tOut = t - fadeInTime - holdTime;
        if (fadeOutTime > 0 && tOut >= 0) {
            if (tOut >= fadeOutTime) return 0;
            float f = 1.0f - tOut / fadeOutTime;
            return Apply(f);
        }
        return 1.0f;
    }

    // Apply the easing curve to a normalized input in [0,1].
    float Apply(float x) const {
        x = x < 0 ? 0 : (x > 1 ? 1 : x);
        switch (curve) {
            case Curve::Smoothstep: return x * x * (3 - 2 * x);
            case Curve::Exponential: return std::pow(x, 2.0f);
            case Curve::Linear:
            default: return x;
        }
    }
};

} // namespace bighero
