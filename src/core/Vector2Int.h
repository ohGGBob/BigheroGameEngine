#pragma once
#include <cmath>

namespace bighero {

// Vector2Int: a minimal 2D integer vector used for grid/tile/atlas coordinates.
// Self-contained, standard-library only.
struct Vector2Int {
    int x = 0;
    int y = 0;

    Vector2Int() = default;
    Vector2Int(int x_, int y_) : x(x_), y(y_) {}

    Vector2Int operator+(const Vector2Int& o) const { return {x + o.x, y + o.y}; }
    Vector2Int operator-(const Vector2Int& o) const { return {x - o.x, y - o.y}; }
    Vector2Int operator*(int s) const { return {x * s, y * s}; }
    Vector2Int operator/(int s) const { return {x / s, y / s}; }
    bool operator==(const Vector2Int& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vector2Int& o) const { return !(*this == o); }

    float Magnitude() const { return std::sqrt((float)(x * x + y * y)); }
    int SqrMagnitude() const { return x * x + y * y; }
    int Dot(const Vector2Int& o) const { return x * o.x + y * o.y; }
    static Vector2Int Min(const Vector2Int& a, const Vector2Int& b) {
        return { a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y };
    }
    static Vector2Int Max(const Vector2Int& a, const Vector2Int& b) {
        return { a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y };
    }
};

} // namespace bighero
