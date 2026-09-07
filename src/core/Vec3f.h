#pragma once
#include <cmath>

namespace bighero {

// 3D float vector.
struct Vec3f {
    float x, y, z;

    Vec3f() : x(0), y(0), z(0) {}
    Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3f operator+(const Vec3f& o) const { return Vec3f(x + o.x, y + o.y, z + o.z); }
    Vec3f operator-(const Vec3f& o) const { return Vec3f(x - o.x, y - o.y, z - o.z); }
    Vec3f operator*(float s) const { return Vec3f(x * s, y * s, z * s); }
    Vec3f operator/(float s) const { float inv = 1.0f / s; return Vec3f(x * inv, y * inv, z * inv); }
    Vec3f operator-() const { return Vec3f(-x, -y, -z); }

    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    float LengthSq() const { return x * x + y * y + z * z; }

    Vec3f Normalized() const {
        float len = Length();
        if (len < 1e-8f) return Vec3f(0, 0, 0);
        return Vec3f(x / len, y / len, z / len);
    }

    static float Dot(const Vec3f& a, const Vec3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    static Vec3f Cross(const Vec3f& a, const Vec3f& b) {
        return Vec3f(a.y * b.z - a.z * b.y,
                     a.z * b.x - a.x * b.z,
                     a.x * b.y - a.y * b.x);
    }

    static Vec3f Lerp(const Vec3f& a, const Vec3f& b, float t) { return a + (b - a) * t; }
};

} // namespace bighero
