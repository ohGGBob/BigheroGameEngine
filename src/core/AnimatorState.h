#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// AnimatorState: one state in an animator state machine — a clip, loop mode,
// speed, and transition list. Pure data + small helpers used by the animator.
class AnimatorState {
public:
    struct Transition { std::size_t to; float duration; };

    AnimatorState() {}
    AnimatorState(const std::string& name, std::size_t clipId)
        : name_(name), clipId_(clipId) {}

    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }
    void SetClipId(std::size_t id) { clipId_ = id; }
    std::size_t ClipId() const { return clipId_; }
    void SetLoop(bool l) { loop_ = l; }
    bool Loop() const { return loop_; }
    void SetSpeed(float s) { speed_ = s; }
    float Speed() const { return speed_; }

    void AddTransition(std::size_t targetStateId, float durationMillis) {
        transitions_.push_back({targetStateId, durationMillis});
    }
    std::size_t TransitionCount() const { return transitions_.size(); }
    bool GetTransition(std::size_t i, Transition& out) const {
        if (i >= transitions_.size()) return false;
        out = transitions_[i]; return true;
    }

    void SetStateId(std::size_t id) { stateId_ = id; }
    std::size_t StateId() const { return stateId_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    std::string name_;
    std::size_t clipId_ = 0, stateId_ = 0;
    bool loop_ = true;
    float speed_ = 1.0f;
    bool enabled_ = true;
    std::vector<Transition> transitions_;
};

} // namespace bighero
