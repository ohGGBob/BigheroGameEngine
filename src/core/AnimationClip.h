#pragma once
#include <vector>
#include <string>
#include <cmath>

namespace bighero {

// Keyframe track + clip for simple float/vector animation.
struct AnimationClip {
    struct Keyframe {
        float time;   // seconds
        float value;  // scalar value (for single-channel clips)
    };
    enum Channel { Scalar, Vec2, Vec3 };

    std::string name;
    Channel channel = Scalar;
    std::vector<Keyframe> keys;
    float duration = 0.0f;

    void AddKey(float time, float value) {
        keys.push_back({time, value});
        if (time > duration) duration = time;
    }
    void Clear() { keys.clear(); duration = 0.0f; }

    // Sample scalar channel at time t (clamped, linear interpolation).
    float Sample(float t) const {
        if (keys.empty()) return 0.0f;
        if (t <= keys.front().time) return keys.front().value;
        if (t >= keys.back().time)  return keys.back().value;
        for (size_t i = 0; i + 1 < keys.size(); ++i) {
            const Keyframe& k0 = keys[i];
            const Keyframe& k1 = keys[i+1];
            if (t >= k0.time && t <= k1.time) {
                float f = (k1.time > k0.time) ? (t - k0.time) / (k1.time - k0.time) : 0.0f;
                return k0.value + (k1.value - k0.value) * f;
            }
        }
        return keys.back().value;
    }

    bool Empty() const { return keys.empty(); }
    int NumKeys() const { return (int)keys.size(); }
};

} // namespace bighero
