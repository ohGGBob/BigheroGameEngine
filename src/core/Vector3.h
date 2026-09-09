#pragma once
#include <cmath>

namespace bighero {

// Vector3: a 3D floating-point vector with the standard arithmetic and
// geometric operations (dot, cross, normalize) used by the engine.
struct Vector3 {
    float x = 0, y = 0, z = 0;

    Vector3() = default;
    Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vector3 operator+(const Vector3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vector3 operator-(const Vector3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vector3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vector3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vector3 operator-() const { return {-x, -y, -z}; }

    float Dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vector3 Cross(const Vector3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    float Magnitude() const { return std::sqrt(x * x + y * y + z * z); }
    float SqrMagnitude() const { return x * x + y * y + z * z; }
    Vector3 Normalized() const {
        float m = Magnitude();
        if (m < 1e-9f) return {0, 0, 0};
        return {x / m, y / m, z / m};
    }
    static Vector3 Lerp(const Vector3& a, const Vector3& b, float t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }
    static float Distance(const Vector3& a, const Vector3& b) { return (a - b).Magnitude(); }
    static const Vector3& Zero() { static Vector3 z(0, 0, 0); return z; }
    static const Vector3& One() { static Vector3 o(1, 1, 1); return o; }
};

} // namespace bighero
