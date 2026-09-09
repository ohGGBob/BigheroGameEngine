#pragma once
#include <cmath>

namespace bighero {

// Vector3Int: a minimal 3D integer vector used for voxel/grid/chunk coordinates.
// Self-contained, standard-library only.
struct Vector3Int {
    int x = 0;
    int y = 0;
    int z = 0;

    Vector3Int() = default;
    Vector3Int(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}

    Vector3Int operator+(const Vector3Int& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vector3Int operator-(const Vector3Int& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vector3Int operator*(int s) const { return {x * s, y * s, z * s}; }
    bool operator==(const Vector3Int& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vector3Int& o) const { return !(*this == o); }

    float Magnitude() const { return std::sqrt((float)(x * x + y * y + z * z)); }
    int SqrMagnitude() const { return x * x + y * y + z * z; }
    int Dot(const Vector3Int& o) const { return x * o.x + y * o.y + z * o.z; }
    static Vector3Int Min(const Vector3Int& a, const Vector3Int& b) {
        return { a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z };
    }
    static Vector3Int Max(const Vector3Int& a, const Vector3Int& b) {
        return { a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z };
    }
};

} // namespace bighero
