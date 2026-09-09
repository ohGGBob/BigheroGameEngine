#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// Mathf: a small collection of common math helper functions used across the
// engine (clamp, lerp, smoothstep, ping-pong, repeat, sign, deg/rad, etc.).
// Standard-library only, self-contained.
class Mathf {
public:
    static constexpr float Pi = 3.14159265358979f;
    static constexpr float TwoPi = 6.28318530717959f;

    static float Clamp(float v, float mn, float mx) {
        if (v < mn) return mn;
        if (v > mx) return mx;
        return v;
    }
    static int ClampInt(int v, int mn, int mx) {
        if (v < mn) return mn;
        if (v > mx) return mx;
        return v;
    }
    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    static float LerpClamped(float a, float b, float t) { return Lerp(a, b, Clamp(t, 0, 1)); }
    static float InverseLerp(float a, float b, float v) {
        if (std::fabs(b - a) < 1e-9f) return 0;
        return Clamp((v - a) / (b - a), 0, 1);
    }
    static float SmoothStep(float e0, float e1, float x) {
        float t = Clamp((x - e0) / (e1 - e0), 0, 1);
        return t * t * (3 - 2 * t);
    }
    static float Repeat(float t, float length) {
        if (length <= 0) return 0;
        float f = t - std::floor(t / length) * length;
        return f < 0 ? f + length : f;
    }
    static float PingPong(float t, float length) {
        if (length <= 0) return 0;
        float f = Repeat(t, length * 2.0f);
        return length - std::fabs(f - length);
    }
    static float Sign(float v) { return v < 0 ? -1.0f : (v > 0 ? 1.0f : 0.0f); }
    static float Abs(float v) { return std::fabs(v); }
    static float Deg2Rad(float deg) { return deg * Pi / 180.0f; }
    static float Rad2Deg(float rad) { return rad * 180.0f / Pi; }
    static float Sin(float v) { return std::sin(v); }
    static float Cos(float v) { return std::cos(v); }
    static float Sqrt(float v) { return std::sqrt(v); }
    static float Min(float a, float b) { return a < b ? a : b; }
    static float Max(float a, float b) { return a > b ? a : b; }
    // Move a value toward a target by maxDelta.
    static float MoveTowards(float current, float target, float maxDelta) {
        float diff = target - current;
        float ad = std::fabs(diff);
        if (ad <= maxDelta) return target;
        return current + Sign(diff) * maxDelta;
    }
};

} // namespace bighero
