#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "AnimationTransition.h"

namespace bighero {

// AnimatorStateMachine: a graph of animation states + transitions.
// Self-contained / std-lib only.
class AnimatorStateMachine {
public:
    void AddState(const AnimationState& s) { states_.push_back(s); }
    size_t StateCount() const { return states_.size(); }
    const AnimationState* FindState(const std::string& name) const {
        for (auto& s : states_) if (s.Name() == name) return &s;
        return nullptr;
    }
    AnimationState* FindState(const std::string& name) {
        for (auto& s : states_) if (s.Name() == name) return &s;
        return nullptr;
    }

    void AddTransition(const AnimationTransition& t) { transitions_.push_back(t); }
    size_t TransitionCount() const { return transitions_.size(); }

    void SetDefaultState(std::string name) { defaultState_ = std::move(name); }
    const std::string& DefaultState() const { return defaultState_; }

    size_t ExitTransitions(int fromState) const {
        size_t n = 0;
        for (auto& t : transitions_) if (t.fromState == fromState) ++n;
        return n;
    }

private:
    std::vector<AnimationState> states_;
    std::vector<AnimationTransition> transitions_;
    std::string defaultState_;
};

} // namespace bighero
