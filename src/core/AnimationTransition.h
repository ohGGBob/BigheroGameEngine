#pragma once
#include "CurveKey.h"
#include <vector>
#include <algorithm>

namespace bighero {

// AnimationTransition: describes a transition between two animation states
// (from/to state ids + duration + blend mode). Self-contained.
struct AnimationTransition {
    int fromState = -1;
    int toState = -1;
    float duration = 0.2f;
    float startTime = 0.0f;   // offset into the 'to' state
    bool loop = false;
    bool interrupts = false;  // can interrupt the 'from' state at any point

    enum class BlendMode { Normal, Additive };

    BlendMode blendMode = BlendMode::Normal;

    AnimationTransition() = default;
    AnimationTransition(int from, int to, float dur)
        : fromState(from), toState(to), duration(dur) {}

    void SetDuration(float d) { duration = d < 0 ? 0 : d; }
    // Evaluate the blend weight at local transition time t in [0,1].
    float Weight(float t) const {
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        return t * t * (3 - 2 * t);   // smoothstep
    }
};

} // namespace bighero
