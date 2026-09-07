#pragma once
#include <cmath>

namespace bighero {

// 2D float vector.
struct Vec2f {
    float x, y;

    Vec2f() : x(0), y(0) {}
    Vec2f(float x_, float y_) : x(x_), y(y_) {}

    Vec2f operator+(const Vec2f& o) const { return Vec2f(x + o.x, y + o.y); }
    Vec2f operator-(const Vec2f& o) const { return Vec2f(x - o.x, y - o.y); }
    Vec2f operator*(float s) const { return Vec2f(x * s, y * s); }
    Vec2f operator/(float s) const { float inv = 1.0f / s; return Vec2f(x * inv, y * inv); }
    Vec2f operator-() const { return Vec2f(-x, -y); }

    float Length() const { return std::sqrt(x * x + y * y); }
    float LengthSq() const { return x * x + y * y; }

    Vec2f Normalized() const {
        float len = Length();
        if (len < 1e-8f) return Vec2f(0, 0);
        return Vec2f(x / len, y / len);
    }

    static float Dot(const Vec2f& a, const Vec2f& b) { return a.x * b.x + a.y * b.y; }
    static float Cross(const Vec2f& a, const Vec2f& b) { return a.x * b.y - a.y * b.x; }
    static Vec2f Lerp(const Vec2f& a, const Vec2f& b, float t) { return a + (b - a) * t; }
};

} // namespace bighero
