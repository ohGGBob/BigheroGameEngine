#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstddef>

namespace bighero {

// Sprite animator: plays a sequence of frames from a sprite sheet / texture
// atlas. Pure timing-driven frame index advancement.
class SpriteAnimator {
public:
    struct Clip {
        std::string name;
        std::vector<int> frames;
        float fps = 12.0f;
        bool loop = true;
    };

    SpriteAnimator() {}

    void AddClip(const std::string& name, const std::vector<int>& frames,
                 float fps = 12.0f, bool loop = true) {
        clips_[name] = {name, frames, fps <= 0 ? 1 : fps, loop};
    }
    bool HasClip(const std::string& name) const { return clips_.count(name) != 0; }
    std::size_t ClipCount() const { return clips_.size(); }

    void Play(const std::string& name) {
        auto it = clips_.find(name);
        if (it == clips_.end()) return;
        current_ = it->second;
        frameIndex_ = 0;
        time_ = 0;
        playing_ = true;
    }
    void Stop() { playing_ = false; }
    bool IsPlaying() const { return playing_; }
    const std::string& CurrentClip() const { return current_.name; }

    // Advance by dt; returns true if current frame changed.
    bool Update(float dt) {
        if (!playing_ || current_.frames.empty()) return false;
        time_ += dt;
        float frameDur = 1.0f / current_.fps;
        if (time_ >= frameDur) {
            time_ = 0;
            ++frameIndex_;
            if (frameIndex_ >= (int)current_.frames.size()) {
                if (current_.loop) frameIndex_ = 0;
                else { frameIndex_ = (int)current_.frames.size() - 1; playing_ = false; }
            }
            return true;
        }
        return false;
    }

    int CurrentFrame() const {
        if (current_.frames.empty()) return -1;
        return current_.frames[(std::size_t)frameIndex_];
    }
    int FrameIndex() const { return frameIndex_; }
    std::size_t FrameCount() const { return current_.frames.size(); }
    void SetTime(float t) { time_ = t; }

private:
    std::map<std::string, Clip> clips_;
    Clip current_;
    int frameIndex_ = 0;
    float time_ = 0;
    bool playing_ = false;
};

} // namespace bighero
