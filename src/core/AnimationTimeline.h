#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// AnimationTimeline: a sequence of timed clips/tracks with a playhead,
// supporting additive playback and per-clip (start, duration) scheduling.
// Standard-library only, self-contained.
class AnimationTimeline {
public:
    struct Clip {
        float start = 0;      // when the clip begins (seconds)
        float duration = 1;   // clip duration
        float weight = 1.0f;  // influence weight
        unsigned id = 0;      // caller-owned clip id
    };

    AnimationTimeline() {}

    void Clear() { clips_.clear(); totalDuration_ = 0; }
    void AddClip(const Clip& c) {
        clips_.push_back(c);
        float end = c.start + c.duration;
        if (end > totalDuration_) totalDuration_ = end;
    }
    std::size_t ClipCount() const { return clips_.size(); }
    const Clip& ClipAt(std::size_t i) const { return clips_[i]; }
    float TotalDuration() const { return totalDuration_; }

    // Query which clips are active at time t; fills their ids + weights.
    void Sample(float t, std::vector<unsigned>& activeIds,
                std::vector<float>& weights) const {
        activeIds.clear(); weights.clear();
        for (auto& c : clips_) {
            if (t >= c.start && t < c.start + c.duration) {
                activeIds.push_back(c.id);
                weights.push_back(c.weight);
            }
        }
    }

    // Normalized total progress in [0,1].
    float NormalizedProgress(float t) const {
        if (totalDuration_ <= 0) return 0;
        float f = t / totalDuration_;
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        return f;
    }

private:
    std::vector<Clip> clips_;
    float totalDuration_ = 0;
};

} // namespace bighero
