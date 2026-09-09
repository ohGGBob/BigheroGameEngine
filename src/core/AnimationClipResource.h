#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// AnimationClipResource: metadata + duration of an animation clip.
// Self-contained / std-lib only.
class AnimationClipResource {
public:
    AnimationClipResource() = default;
    AnimationClipResource(std::string name, float duration)
        : name_(std::move(name)), duration_(duration) {}

    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }
    void SetDuration(float d) { duration_ = d < 0 ? 0 : d; }
    float Duration() const { return duration_; }
    void SetLoop(bool b) { loop_ = b; }
    bool Loop() const { return loop_; }
    void SetFrameCount(uint32_t c) { frameCount_ = c; }
    uint32_t FrameCount() const { return frameCount_; }
    void SetFramesPerSecond(float fps) { fps_ = fps > 0 ? fps : 1; }
    float FramesPerSecond() const { return fps_; }

    bool IsValid() const { return duration_ > 0.0f; }
    float DurationSeconds() const { return duration_; }
    uint32_t EstimatedFrameCount() const { return (uint32_t)(duration_ * fps_ + 0.5f); }

private:
    std::string name_;
    float duration_ = 0, fps_ = 30;
    uint32_t frameCount_ = 0;
    bool loop_ = false;
};

} // namespace bighero
