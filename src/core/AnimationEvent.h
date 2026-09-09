#pragma once
#include <cstddef>
#include <string>

namespace bighero {

// AnimationEvent: a named time-tagged event fired while an animation clip
// plays. Holds a normalized time, a name, and an optional parameter. Pure
// data container consumed by the animation scheduler.
class AnimationEvent {
public:
    AnimationEvent() {}
    AnimationEvent(float time, const char* name) : time_(time), name_(name ? name : "") {}

    void SetTime(float t) { time_ = t < 0 ? 0 : t; }
    float Time() const { return time_; }
    void SetName(const char* n) { name_ = n ? n : ""; }
    const char* Name() const { return name_.c_str(); }
    void SetFloatParameter(float f) { floatParam_ = f; hasFloat_ = true; }
    bool GetFloatParameter(float& out) const { out = floatParam_; return hasFloat_; }
    void SetIntParameter(int i) { intParam_ = i; hasInt_ = true; }
    bool GetIntParameter(int& out) const { out = intParam_; return hasInt_; }

    void SetClipId(std::size_t id) { clipId_ = id; }
    std::size_t ClipId() const { return clipId_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    // Return true if the event should fire when clip time crosses this marker.
    bool ShouldFire(float prevTime, float curTime) const {
        if (curTime >= time_ && prevTime < time_) return true;
        if (curTime < prevTime && time_ <= curTime) return true; // wrapped
        return false;
    }

private:
    float time_ = 0.0f;
    std::string name_;
    float floatParam_ = 0.0f;
    bool hasFloat_ = false;
    int intParam_ = 0;
    bool hasInt_ = false;
    std::size_t clipId_ = 0;
    bool enabled_ = true;
};

} // namespace bighero
