#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// Tween state for a single scalar property (for animation/interpolation).
struct TweenState {
    enum Easing { Linear, EaseIn, EaseOut, EaseInOut };

    float start = 0, end = 0;
    float duration = 1.0f;
    float elapsed = 0;
    Easing easing = Linear;
    bool loop = false;
    bool pingpong = false;
    int loopCount = 0;         // -1 = infinite
    int completedLoops = 0;

    void Reset() { elapsed = 0; completedLoops = 0; }
    void Set(float s, float e, float d, Easing ez = Linear) { start = s; end = e; duration = d > 0 ? d : 1.0f; easing = ez; Reset(); }

    // Advance by dt seconds; returns true while still animating (or looping).
    bool Update(float dt) {
        if (duration <= 0) { elapsed = duration; return false; }
        elapsed += dt;
        if (elapsed < duration) return true;
        // handle completed frames
        float exceed = elapsed - duration;
        if (loop || pingpong) {
            if (loopCount == -1 || completedLoops < loopCount) {
                ++completedLoops;
                if (pingpong) { std::swap(start, end); }
                elapsed = exceed;
                if (elapsed < duration) return true;
            }
            return false;
        }
        elapsed = duration;
        return false;
    }

    // Current interpolated value (uses easing).
    float Value() const {
        if (duration <= 0) return end;
        float t = elapsed / duration;
        if (t < 0) t = 0; if (t > 1) t = 1;
        t = ApplyEasing(t);
        return start + (end - start) * t;
    }

    bool Finished() const {
        if (loop || pingpong) { if (loopCount == -1) return false; return completedLoops >= loopCount && elapsed >= duration; }
        return elapsed >= duration;
    }

private:
    float ApplyEasing(float t) const {
        switch (easing) {
            case EaseIn: return t * t;
            case EaseOut: return 1 - (1 - t) * (1 - t);
            case EaseInOut: return t < 0.5f ? 2*t*t : 1 - 2*(1-t)*(1-t);
            case Linear: default: return t;
        }
    }
};

} // namespace bighero
