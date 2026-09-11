#pragma once
#include "AnimationCurveFrame_v2.h"

#include <cstdint>
#include <vector>

namespace bighero {

// AnimationClipData: raw track/channel storage for an animation clip.
// Self-contained / std-lib only.
class AnimationClipData {
public:
    struct Channel {
        int32_t target = -1;
        std::vector<AnimationCurveFrame> frames;
    };

    void SetDuration(float d) { duration_ = d < 0 ? 0 : d; }
    float Duration() const { return duration_; }
    void SetLoop(bool b) { loop_ = b; }
    bool Loop() const { return loop_; }

    size_t AddChannel() { channels_.emplace_back(); return channels_.size() - 1; }
    size_t ChannelCount() const { return channels_.size(); }
    Channel& ChannelAt(size_t i) { return channels_[i]; }
    const Channel& ChannelAt(size_t i) const { return channels_[i]; }

    void Clear() { channels_.clear(); duration_ = 0; loop_ = false; }
    bool Empty() const { return channels_.empty(); }

    // Evaluate all channels at a normalized time t in [0,1] is left to caller;
    // this provides longest-duration / channel bounding helpers.
    float LongestTrackTime() const {
        float longest = 0;
        for (auto& c : channels_) if (!c.frames.empty())
            if (c.frames.back().Time() > longest) longest = c.frames.back().Time();
        return longest;
    }

private:
    std::vector<Channel> channels_;
    float duration_ = 0;
    bool loop_ = false;
};

} // namespace bighero
