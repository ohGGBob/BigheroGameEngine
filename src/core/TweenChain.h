#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// TweenChain: sequential playback of a list of one-shot tween segments.
// Each segment has a duration, an easing index, and a linear value range
// [from,to]. Advancing by dt moves the chain forward and exposes the
// current segment's interpolated value. Pure stdlib.
class TweenChain {
public:
    enum class Easing { Linear, EaseIn, EaseOut, EaseInOut };

    struct Segment {
        float duration;
        float from, to;
        Easing easing;
    };

    TweenChain() {}

    void Add(float duration, float from, float to, Easing e = Easing::Linear) {
        segs_.push_back({duration < 0 ? 0 : duration, from, to, e});
    }
    std::size_t SegmentCount() const { return segs_.size(); }

    void Play() { playing_ = true; segIndex_ = 0; segTime_ = 0; finished_ = false; }
    void Stop() { playing_ = false; }
    void Rewind() { segIndex_ = 0; segTime_ = 0; finished_ = false; }
    bool IsPlaying() const { return playing_; }
    bool IsFinished() const { return finished_; }

    // Advance chain by dt; returns true if segment index advanced.
    bool Update(float dt) {
        if (!playing_ || segs_.empty()) return false;
        float remaining = dt;
        bool advanced = false;
        // consume time across boundaries
        while (remaining > 0 && segIndex_ < segs_.size() && !finished_) {
            float dur = segs_[segIndex_].duration;
            float left = dur - segTime_;
            if (remaining < left) { segTime_ += remaining; remaining = 0; }
            else { remaining -= left; ++segIndex_; segTime_ = 0; advanced = true; }
        }
        if (segIndex_ >= segs_.size()) { playing_ = false; finished_ = true; }
        return advanced;
    }

    // Current interpolated value in [0,1] normalized of the active segment.
    float CurrentValue() const {
        if (segs_.empty()) return 0.0f;
        if (finished_) {
            const Segment& s = segs_.back();
            return s.to;
        }
        std::size_t i = segIndex_ < segs_.size() ? segIndex_ : segs_.size() - 1;
        const Segment& s = segs_[i];
        float t = s.duration <= 0 ? 1.0f : segTime_ / s.duration;
        if (t > 1) t = 1;
        if (t < 0) t = 0;
        float e = ApplyEasing(t, s.easing);
        return s.from + (s.to - s.from) * e;
    }

    float TotalDuration() const {
        float d = 0;
        for (auto& s : segs_) d += s.duration;
        return d;
    }

private:
    static float ApplyEasing(float t, Easing e) {
        switch (e) {
            case Easing::EaseIn:   return t * t;
            case Easing::EaseOut:  return 1 - (1 - t) * (1 - t);
            case Easing::EaseInOut: return t < 0.5f ? 2*t*t : 1 - 2*(1-t)*(1-t);
            case Easing::Linear:
            default: return t;
        }
    }

    std::vector<Segment> segs_;
    std::size_t segIndex_ = 0;
    float segTime_ = 0;
    bool playing_ = false;
    bool finished_ = false;
};

} // namespace bighero
