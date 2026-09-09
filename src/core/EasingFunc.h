#pragma once
#include <cmath>

namespace bighero {

// EasingFunc: a curated set of easing curves (ease-in, ease-out, ease-in-out,
// back, bounce, elastic, smoothstep) mapping a normalized t in [0,1] to a
// eased value. Standard-library only, self-contained.
class EasingFunc {
public:
    enum class Type {
        Linear, EaseInQuad, EaseOutQuad, EaseInOutQuad,
        EaseInCubic, EaseOutCubic, EaseInOutCubic,
        EaseInBack, EaseOutBack, EaseInOutBack,
        EaseOutBounce, EaseInElastic, Smoothstep
    };

    EasingFunc() {}
    explicit EasingFunc(Type type) : type_(type) {}

    void SetType(Type t) { type_ = t; }
    Type GetType() const { return type_; }

    float Apply(float t) const {
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        switch (type_) {
            case Type::Linear:          return t;
            case Type::EaseInQuad:      return t * t;
            case Type::EaseOutQuad:     return t * (2 - t);
            case Type::EaseInOutQuad:   return t < 0.5f ? 2*t*t : -1 + (4-2*t)*t;
            case Type::EaseInCubic:     return t * t * t;
            case Type::EaseOutCubic:    return 1 - std::pow(1 - t, 3);
            case Type::EaseInOutCubic:  return t < 0.5f ? 4*t*t*t : 1 - std::pow(-2*t+2, 3)/2;
            case Type::EaseInBack:      return (1.70158f+1)*t*t*t - 1.70158f*t*t;
            case Type::EaseOutBack:     return 1 + (1.70158f+1)*std::pow(t-1,3) + 1.70158f*std::pow(t-1,2);
            case Type::EaseInOutBack: {
                const float c1=1.70158f, c2=c1*1.525f;
                return t < 0.5f ? (std::pow(2*t,2)*((c2+1)*2*t - c2))/2
                                : (std::pow(2*t-2,2)*((c2+1)*(t*2-2)+c2)+2)/2;
            }
            case Type::EaseOutBounce:   return OutBounce(t);
            case Type::EaseInElastic: {
                const float c4 = 6.2831853f/3;
                if (t <= 0) return 0;
                if (t >= 1) return 1;
                return -std::pow(2, 10*t-10) * std::sin((t*10-10.75f)*c4);
            }
            case Type::Smoothstep:      return t * t * (3 - 2 * t);
            default:                    return t;
        }
    }

private:
    static float OutBounce(float t) {
        const float n1 = 7.5625f, d1 = 2.75f;
        if (t < 1/d1) return n1*t*t;
        if (t < 2/d1) { t -= 1.5f/d1; return n1*t*t + 0.75f; }
        if (t < 2.5f/d1) { t -= 2.25f/d1; return n1*t*t + 0.9375f; }
        t -= 2.625f/d1; return n1*t*t + 0.984375f;
    }
    Type type_ = Type::Linear;
};

} // namespace bighero
