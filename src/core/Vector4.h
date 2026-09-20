#pragma once
#include <cmath>
#include <cstddef>

namespace bighero
{

// Vector4: a 4-component floating-point vector with basic math helpers.
// Fully self-contained; used across rendering, math, and animation code.
class Vector4
{
  public:
    float x, y, z, w;

    Vector4() : x(0), y(0), z(0), w(0) {}
    Vector4(float X, float Y, float Z, float W) : x(X), y(Y), z(Z), w(W) {}
    explicit Vector4(float s) : x(s), y(s), z(s), w(s) {}

    float operator[](std::size_t i) const
    {
        const float* v = &x;
        return v[i];
    }
    float& operator[](std::size_t i)
    {
        float* v = &x;
        return v[i];
    }

    Vector4 operator+(const Vector4& o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vector4 operator-(const Vector4& o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    Vector4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    Vector4 operator/(float s) const
    {
        float inv = 1.0f / s;
        return {x * inv, y * inv, z * inv, w * inv};
    }
    Vector4& operator+=(const Vector4& o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        w += o.w;
        return *this;
    }

    Vector4 operator-() const { return {-x, -y, -z, -w}; }

    float Dot(const Vector4& o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
    float Length() const { return std::sqrt(Dot(*this)); }
    float LengthSquared() const { return Dot(*this); }
    Vector4 Normalized() const
    {
        float l = Length();
        if (l < 1e-8f)
            return Vector4(0, 0, 0, 0);
        return (*this) / l;
    }

    static Vector4 Lerp(const Vector4& a, const Vector4& b, float t)
    {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
    }
    static Vector4 Min(const Vector4& a, const Vector4& b)
    {
        return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z, a.w < b.w ? a.w : b.w};
    }
    static Vector4 Max(const Vector4& a, const Vector4& b)
    {
        return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z, a.w > b.w ? a.w : b.w};
    }

    static float Distance(const Vector4& a, const Vector4& b) { return (a - b).Length(); }
    bool IsZero(float eps = 1e-6f) const
    {
        return std::fabs(x) < eps && std::fabs(y) < eps && std::fabs(z) < eps && std::fabs(w) < eps;
    }
    void Set(float X, float Y, float Z, float W)
    {
        x = X;
        y = Y;
        z = Z;
        w = W;
    }
    void Zero() { Set(0, 0, 0, 0); }
};

inline Vector4 operator*(float s, const Vector4& v)
{
    return v * s;
}

} // namespace bighero
