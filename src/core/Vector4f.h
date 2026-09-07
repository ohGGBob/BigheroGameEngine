#pragma once
#include <cmath>

namespace bighero {

// 4D float vector usable for homogeneous coordinates, colors, etc.
struct Vector4f {
    float x, y, z, w;

    Vector4f() : x(0), y(0), z(0), w(0) {}
    Vector4f(float x_, float y_, float z_, float w_ = 1.0f) : x(x_), y(y_), z(z_), w(w_) {}

    Vector4f operator+(const Vector4f& o) const { return Vector4f(x + o.x, y + o.y, z + o.z, w + o.w); }
    Vector4f operator-(const Vector4f& o) const { return Vector4f(x - o.x, y - o.y, z - o.z, w - o.w); }
    Vector4f operator*(float s) const { return Vector4f(x * s, y * s, z * s, w * s); }
    Vector4f operator/(float s) const { float inv = 1.0f / s; return Vector4f(x * inv, y * inv, z * inv, w * inv); }

    float Length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float LengthSq() const { return x * x + y * y + z * z + w * w; }

    Vector4f Normalized() const {
        float len = Length();
        if (len < 1e-8f) return Vector4f(0, 0, 0, 0);
        return Vector4f(x / len, y / len, z / len, w / len);
    }

    static float Dot(const Vector4f& a, const Vector4f& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    static Vector4f Lerp(const Vector4f& a, const Vector4f& b, float t) {
        return a + (b - a) * t;
    }

    // Homogeneous divide to get 3D position.
    void ToVec3(float& ox, float& oy, float& oz) const {
        float invW = (std::abs(w) < 1e-8f) ? 1.0f : (1.0f / w);
        ox = x * invW; oy = y * invW; oz = z * invW;
    }
};

} // namespace bighero
