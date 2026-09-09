#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// Keyframe: one keyframe in an animation curve/track — time, value, and
// in/out tangents. Pure data record used by curve and clip sampling.
class Keyframe {
public:
    Keyframe() {}
    Keyframe(float time, float value) : time_(time), value_(value) {}

    void SetTime(float t) { time_ = t; }
    float Time() const { return time_; }
    void SetValue(float v) { value_ = v; }
    float Value() const { return value_; }
    void SetInTangent(float t) { inTangent_ = t; }
    float InTangent() const { return inTangent_; }
    void SetOutTangent(float t) { outTangent_ = t; }
    float OutTangent() const { return outTangent_; }

    void SetWeights(float inW, float outW) { inWeight_=inW; outWeight_=outW; }
    void Weights(float& inW, float& outW) const { inW=inWeight_; outW=outWeight_; }
    void SetBreak(bool b) { break_ = b; }
    bool IsBreak() const { return break_; }

private:
    float time_=0, value_=0;
    float inTangent_=0, outTangent_=0;
    float inWeight_=0.33f, outWeight_=0.33f;
    bool break_=false;
};

} // namespace bighero
