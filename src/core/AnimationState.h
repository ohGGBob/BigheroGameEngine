#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// Data-driven animation state: named tracks each holding keyframes of a
// scalar value with easing, sampled over duration. Building block for
// property/track animation (no dependency on a full AnimationController).
class AnimationState {
public:
    enum class Easing { Linear, EaseIn, EaseOut, EaseInOut };

    struct Keyframe { float time; float value; Easing easing; };

    void AddTrack(const char* name) {
        tracks_.push_back({std::string(name), {}});
    }
    int FindTrack(const char* name) const {
        for (std::size_t i = 0; i < tracks_.size(); ++i)
            if (tracks_[i].name == name) return (int)i;
        return -1;
    }

    void AddKeyframe(const char* track, float time, float value, Easing e = Easing::Linear) {
        int i = FindTrack(track);
        if (i < 0) return;
        tracks_[(std::size_t)i].keys.push_back({time, value, e});
        // keep sorted by time
        auto& ks = tracks_[(std::size_t)i].keys;
        for (std::size_t a = ks.size() - 1; a > 0 && ks[a].time < ks[a-1].time; --a)
            std::swap(ks[a], ks[a-1]);
    }

    // Sample a track at time t (clamped to first/last key).
    float Sample(const char* track, float t) const {
        int i = FindTrack(track);
        if (i < 0) return 0;
        const auto& ks = tracks_[(std::size_t)i].keys;
        if (ks.empty()) return 0;
        if (t <= ks.front().time) return ks.front().value;
        if (t >= ks.back().time)  return ks.back().value;
        for (std::size_t k = 0; k + 1 < ks.size(); ++k) {
            const Keyframe& a = ks[k];
            const Keyframe& b = ks[k+1];
            if (t >= a.time && t <= b.time) {
                float f = (b.time > a.time) ? (t - a.time) / (b.time - a.time) : 0;
                f = ApplyEase(a.easing, f);
                return a.value + (b.value - a.value) * f;
            }
        }
        return ks.back().value;
    }

    std::size_t TrackCount() const { return tracks_.size(); }

private:
    float ApplyEase(Easing e, float t) const {
        switch (e) {
            case Easing::Linear:   return t;
            case Easing::EaseIn:   return t * t;
            case Easing::EaseOut:  return t * (2.0f - t);
            case Easing::EaseInOut: return t * t * (3.0f - 2.0f * t);
        }
        return t;
    }
    struct Track { std::string name; std::vector<Keyframe> keys; };
    std::vector<Track> tracks_;
};

} // namespace bighero
