#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// Math utility functions (stateless).
class MathUtils {
public:
    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TwoPi = 2.0f * PI;

    static float DegToRad(float deg) { return deg * PI / 180.0f; }
    static float RadToDeg(float rad) { return rad * 180.0f / PI; }

    static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    static float SmoothStep(float edge0, float edge1, float x) {
        float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    static float Repeat(float t, float length) {
        if (length <= 0) return 0.0f;
        float r = std::fmod(t, length);
        return r < 0 ? r + length : r;
    }

    static float PingPong(float t, float length) {
        if (length <= 0) return 0.0f;
        t = Repeat(t, length * 2.0f);
        return length - std::fabs(t - length);
    }

    static float Min(float a, float b) { return a < b ? a : b; }
    static float Max(float a, float b) { return a > b ? a : b; }
    static int Min(int a, int b) { return a < b ? a : b; }
    static int Max(int a, int b) { return a > b ? a : b; }

    static float Abs(float v) { return std::fabs(v); }
    static int Abs(int v) { return v < 0 ? -v : v; }

    static float Sign(float v) { return v > 0 ? 1.0f : (v < 0 ? -1.0f : 0.0f); }
    static int Sign(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

    static float Square(float v) { return v * v; }

    static float WrapAngle(float rad) {
        rad = std::fmod(rad, TwoPi);
        if (rad < -PI) rad += TwoPi;
        if (rad > PI) rad -= TwoPi;
        return rad;
    }

    static float MoveTowards(float current, float target, float maxDelta) {
        if (std::fabs(target - current) <= maxDelta) return target;
        return current + Sign(target - current) * maxDelta;
    }
};

} // namespace bighero
