#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// ShapeInterpolator: morphs between shapes/positions over a normalized t,
// with easing options and hold/ping-pong modes. Standard-library only,
// self-contained.
class ShapeInterpolator {
public:
    enum class Easing { Linear, EaseIn, EaseOut, EaseInOut, Smoothstep };
    enum class Loop { Clamp, Repeat, PingPong };

    ShapeInterpolator() {}

    void SetEasing(Easing e) { easing_ = e; }
    void SetLoop(Loop l) { loop_ = l; }

    // Map raw t in [0,1] through the loop mode + easing.
    float Apply(float t) const {
        if (loop_ == Loop::Repeat) { t -= std::floor(t); }
        else if (loop_ == Loop::PingPong) {
            float f = t - std::floor(t);
            t = (int(std::floor(t)) & 1) ? (1.0f - f) : f;
        }
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        return Ease(t);
    }

    // Linearly interpolate a value with easing applied.
    float Lerp(float a, float b, float rawT) const {
        float t = Apply(rawT);
        return a + (b - a) * t;
    }

    // Interpolate a 2D point.
    void LerpPoint(float ax, float ay, float bx, float by, float rawT,
                   float& ox, float& oy) const {
        float t = Apply(rawT);
        ox = ax + (bx - ax) * t;
        oy = ay + (by - ay) * t;
    }

    // Interpolate a 3D point.
    void LerpPoint3(float ax, float ay, float az, float bx, float by, float bz,
                    float rawT, float& ox, float& oy, float& oz) const {
        float t = Apply(rawT);
        ox = ax + (bx - ax) * t;
        oy = ay + (by - ay) * t;
        oz = az + (bz - az) * t;
    }

private:
    float Ease(float t) const {
        switch (easing_) {
            case Easing::EaseIn:    return t * t;
            case Easing::EaseOut:   return t * (2.0f - t);
            case Easing::EaseInOut: return t < 0.5f ? 2.0f*t*t : -1.0f + (4.0f - 2.0f*t)*t;
            case Easing::Smoothstep: return t*t*(3.0f - 2.0f*t);
            case Easing::Linear:
            default: return t;
        }
    }
    Easing easing_ = Easing::Linear;
    Loop loop_ = Loop::Clamp;
};

} // namespace bighero
