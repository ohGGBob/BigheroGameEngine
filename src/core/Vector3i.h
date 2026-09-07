#pragma once

namespace bighero {

// 3D integer vector.
struct Vector3i {
    int x, y, z;

    Vector3i() : x(0), y(0), z(0) {}
    Vector3i(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}

    Vector3i operator+(const Vector3i& o) const { return Vector3i(x + o.x, y + o.y, z + o.z); }
    Vector3i operator-(const Vector3i& o) const { return Vector3i(x - o.x, y - o.y, z - o.z); }
    Vector3i operator*(int s) const { return Vector3i(x * s, y * s, z * s); }
    bool operator==(const Vector3i& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vector3i& o) const { return !(*this == o); }

    static int Dot(const Vector3i& a, const Vector3i& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    static Vector3i Min(const Vector3i& a, const Vector3i& b) {
        return Vector3i(a.x<b.x?a.x:b.x, a.y<b.y?a.y:b.y, a.z<b.z?a.z:b.z);
    }
    static Vector3i Max(const Vector3i& a, const Vector3i& b) {
        return Vector3i(a.x>b.x?a.x:b.x, a.y>b.y?a.y:b.y, a.z>b.z?a.z:b.z);
    }
};

} // namespace bighero
