#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>

namespace bighero {

// EasingCurve: a parametric easing curve defined by control keyframes mapping
// t[0,1] -> value. Supports sampling with optional smoothing.
// Self-contained, std-lib only.
class EasingCurve {
public:
    struct Key {
        float t = 0;
        float value = 0;
        Key() = default;
        Key(float time, float v) : t(time), value(v) {}
    };

    void AddKey(float t, float v) { keys_.emplace_back(t, v); Sort(); }
    void Clear() { keys_.clear(); }
    size_t KeyCount() const { return keys_.size(); }

    // Sample value at t (linear interpolation between keys).
    float Sample(float t) const {
        if (keys_.empty()) return 0;
        if (t <= keys_.front().t) return keys_.front().value;
        if (t >= keys_.back().t) return keys_.back().value;
        for (size_t i = 1; i < keys_.size(); ++i) {
            if (t <= keys_[i].t) {
                const Key& a = keys_[i-1];
                const Key& b = keys_[i];
                if (b.t <= a.t) return b.value;
                float f = (t - a.t) / (b.t - a.t);
                return a.value + (b.value - a.value) * f;
            }
        }
        return keys_.back().value;
    }

    // Smoothstep interpolation between keys (ease in/out).
    float SampleSmooth(float t) const {
        if (keys_.empty()) return 0;
        if (t <= keys_.front().t) return keys_.front().value;
        if (t >= keys_.back().t) return keys_.back().value;
        for (size_t i = 1; i < keys_.size(); ++i) {
            if (t <= keys_[i].t) {
                const Key& a = keys_[i-1];
                const Key& b = keys_[i];
                if (b.t <= a.t) return b.value;
                float f = (t - a.t) / (b.t - a.t);
                f = f * f * (3.0f - 2.0f * f);
                return a.value + (b.value - a.value) * f;
            }
        }
        return keys_.back().value;
    }

    const Key& KeyAt(size_t i) const { return keys_[i]; }
    float StartValue() const { return keys_.empty() ? 0 : keys_.front().value; }
    float EndValue() const { return keys_.empty() ? 0 : keys_.back().value; }

private:
    void Sort() {
        std::sort(keys_.begin(), keys_.end(), [](const Key& a, const Key& b){ return a.t < b.t; });
    }
    std::vector<Key> keys_;
};

} // namespace bighero
