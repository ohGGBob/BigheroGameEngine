#pragma once
#include <cmath>

namespace bighero {

// Vector2: a 2D floating-point vector with the standard arithmetic and
// geometric operations used by the engine. Self-contained, std-lib only.
struct Vector2 {
    float x = 0, y = 0;

    Vector2() = default;
    Vector2(float x_, float y_) : x(x_), y(y_) {}

    Vector2 operator+(const Vector2& o) const { return {x + o.x, y + o.y}; }
    Vector2 operator-(const Vector2& o) const { return {x - o.x, y - o.y}; }
    Vector2 operator*(float s) const { return {x * s, y * s}; }
    Vector2 operator/(float s) const { return {x / s, y / s}; }
    Vector2 operator-() const { return {-x, -y}; }
    bool operator==(const Vector2& o) const { return x == o.x && y == o.y; }

    float Dot(const Vector2& o) const { return x * o.x + y * o.y; }
    float Cross(const Vector2& o) const { return x * o.y - y * o.x; }
    float Magnitude() const { return std::sqrt(x * x + y * y); }
    float SqrMagnitude() const { return x * x + y * y; }
    Vector2 Normalized() const {
        float m = Magnitude();
        if (m < 1e-9f) return {0, 0};
        return {x / m, y / m};
    }
    static Vector2 Lerp(const Vector2& a, const Vector2& b, float t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    }
    static Vector2 Min(const Vector2& a, const Vector2& b) {
        return { a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y };
    }
    static Vector2 Max(const Vector2& a, const Vector2& b) {
        return { a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y };
    }
    static float Distance(const Vector2& a, const Vector2& b) {
        return (a - b).Magnitude();
    }
    static const Vector2& Zero() { static Vector2 z(0, 0); return z; }
    static const Vector2& One() { static Vector2 o(1, 1); return o; }
};

} // namespace bighero
