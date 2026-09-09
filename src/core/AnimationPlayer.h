#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <cstddef>

namespace bighero {

// Animation player: plays named clips from a clip registry, advancing a time
// cursor and invoking a per-frame sample callback. Building block for anim.
class AnimationPlayer {
public:
    using SampleFn = std::function<void(const std::string& track, float time)>;

    struct Clip {
        std::string name;
        float duration = 1.0f;
        bool loop = true;
    };

    void AddClip(const std::string& name, float duration, bool loop = true) {
        clips_[name] = {name, duration <= 0 ? 0.001f : duration, loop};
    }
    bool HasClip(const std::string& name) const { return clips_.count(name) != 0; }
    std::size_t ClipCount() const { return clips_.size(); }

    // Play a clip from offset.
    void Play(const std::string& name, float from = 0.0f) {
        auto it = clips_.find(name);
        if (it == clips_.end()) return;
        current_ = it->second;
        time_ = from < 0 ? 0 : from;
        playing_ = true;
    }
    void Stop() { playing_ = false; }
    bool IsPlaying() const { return playing_; }
    const std::string& CurrentClip() const { return current_.name; }
    float Time() const { return time_; }
    float Duration() const { return current_.duration; }

    void SetSampleCallback(SampleFn fn) { callback_ = std::move(fn); }

    // Advance by dt; returns true if the current clip just looped/finished.
    bool Update(float dt) {
        if (!playing_) return false;
        bool wrapped = false;
        time_ += dt;
        if (time_ >= current_.duration) {
            if (current_.loop) {
                time_ = std::fmod(time_, current_.duration);
                wrapped = true;
            } else {
                time_ = current_.duration;
                playing_ = false;
                wrapped = true;
            }
        }
        if (callback_) callback_(current_.name, time_);
        return wrapped;
    }

    // Normalized time in [0,1).
    float Normalized() const { return current_.duration > 0 ? time_ / current_.duration : 0; }

private:
    std::map<std::string, Clip> clips_;
    Clip current_;
    float time_ = 0;
    bool playing_ = false;
    SampleFn callback_;
};

} // namespace bighero
