#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace bighero {

// AnimationState: a named animation state with a clip and playback params.
// Self-contained / std-lib only.
class AnimationState {
public:
    AnimationState() = default;
    AnimationState(std::string name, std::string clipName)
        : name_(std::move(name)), clipName_(std::move(clipName)) {}

    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }
    void SetClipName(std::string c) { clipName_ = std::move(c); }
    const std::string& ClipName() const { return clipName_; }
    void SetSpeed(float s) { speed_ = s; }
    float Speed() const { return speed_; }
    void SetLoop(bool b) { loop_ = b; }
    bool Loop() const { return loop_; }

    void SetTime(float t) { time_ = t < 0 ? 0 : t; }
    float Time() const { return time_; }
    void Advance(float dt) { time_ += dt * speed_; }
    bool IsValid() const { return !name_.empty() && !clipName_.empty(); }

private:
    std::string name_, clipName_;
    float speed_ = 1.0f, time_ = 0.0f;
    bool loop_ = false;
};

} // namespace bighero
