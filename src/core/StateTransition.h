#pragma once
#include <string>
#include <cstddef>
#include <cmath>

namespace bighero {

// StateTransition: a directed transition between two animation states with a
// trigger condition, cross-fade duration and optional exit time. Standard-
// library only, self-contained.
class StateTransition {
public:
    enum class Trigger { Never, OnParameter, OnTime, Immediate };

    StateTransition() {}
    StateTransition(unsigned from, unsigned to, float duration = 0.2f)
        : from_(from), to_(to), duration_(duration < 0 ? 0 : duration) {}

    void SetFromTo(unsigned from, unsigned to) { from_=from; to_=to; }
    unsigned From() const { return from_; }
    unsigned To() const { return to_; }
    void SetDuration(float d) { duration_ = d < 0 ? 0 : d; }
    float Duration() const { return duration_; }

    void SetTrigger(Trigger t) { trigger_ = t; }
    Trigger GetTrigger() const { return trigger_; }

    void SetParameterName(const char* n) { paramName_ = n ? n : ""; }
    const std::string& ParameterName() const { return paramName_; }
    void SetThreshold(float t) { threshold_ = t; }

    void SetExitTime(float t) { exitTime_ = t < 0 ? 0 : (t > 1 ? 1 : t); }
    float ExitTime() const { return exitTime_; }

    // Should this transition fire given the current state's normalized time and
    // an optional parameter value?
    bool ShouldFire(float stateNormalizedTime, float paramValue) const {
        switch (trigger_) {
            case Trigger::Immediate: return true;
            case Trigger::OnTime: return stateNormalizedTime >= exitTime_;
            case Trigger::OnParameter: return IsOnParameter(paramValue);
            case Trigger::Never: default: return false;
        }
    }

    // For a parameter trigger (boolean or >= threshold).
    bool IsOnParameter(float paramValue) const {
        if (threshold_ > 0.5f) return paramValue >= threshold_;
        return paramValue > 0.1f;   // boolean-ish
    }

private:
    unsigned from_=0, to_=0;
    float duration_=0.2f;
    Trigger trigger_ = Trigger::Immediate;
    std::string paramName_;
    float threshold_ = 0.5f;
    float exitTime_ = 0.9f;
};

} // namespace bighero
