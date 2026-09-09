#pragma once
#include <vector>
#include <string>
#include <cstddef>
#include <cmath>

namespace bighero {

// AnimatorController: a parameterized state machine driving animation layers,
// evaluating transitions between clips based on scalar/boolean parameters.
// Standard-library only, self-contained.
class AnimatorController {
public:
    struct State {
        unsigned id = 0;
        float normalizedTime = 0;
        float speed = 1.0f;
        bool loop = true;
    };

    AnimatorController() {}

    void AddState(unsigned id) {
        for (auto& s : states_) if (s.id == id) return;
        states_.push_back({id, 0.0f, 1.0f, true});
    }
    std::size_t StateCount() const { return states_.size(); }
    const State& StateAt(std::size_t i) const { return states_[i]; }

    void SetParameter(const char* name, float v) {
        for (auto& p : params_) if (p.name == (name?name:"")) { p.value = v; p.set = true; return; }
        params_.push_back({ name?name:"", v, true });
    }
    float GetParameter(const char* name, float def = 0.0f) const {
        for (auto& p : params_) if (p.name == (name?name:"")) return p.value;
        return def;
    }
    bool HasParameter(const char* name) const {
        for (auto& p : params_) if (p.name == (name?name:"")) return true;
        return false;
    }

    // Set the current state index.
    void SetState(std::size_t index) {
        if (index < states_.size()) current_ = index;
    }
    std::size_t CurrentState() const { return current_; }
    unsigned CurrentStateId() const { return states_[current_].id; }

    // Advance the current state's normalized time.
    void Advance(float dt, float stateDuration) {
        State& s = states_[current_];
        s.normalizedTime += dt * s.speed / (stateDuration > 0 ? stateDuration : 1.0f);
        if (s.loop) {
            s.normalizedTime -= std::floor(s.normalizedTime);
        } else if (s.normalizedTime > 1.0f) {
            s.normalizedTime = 1.0f;
        }
    }

private:
    struct Param { std::string name; float value; bool set; };
    std::vector<State> states_;
    std::vector<Param> params_;
    std::size_t current_ = 0;
};

} // namespace bighero
