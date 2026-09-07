#pragma once

namespace bighero {

// 2D integer vector.
struct Vector2i {
    int x, y;

    Vector2i() : x(0), y(0) {}
    Vector2i(int x_, int y_) : x(x_), y(y_) {}

    Vector2i operator+(const Vector2i& o) const { return Vector2i(x + o.x, y + o.y); }
    Vector2i operator-(const Vector2i& o) const { return Vector2i(x - o.x, y - o.y); }
    Vector2i operator*(int s) const { return Vector2i(x * s, y * s); }
    bool operator==(const Vector2i& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vector2i& o) const { return !(*this == o); }

    static int Dot(const Vector2i& a, const Vector2i& b) { return a.x * b.x + a.y * b.y; }
    static Vector2i Min(const Vector2i& a, const Vector2i& b) {
        return Vector2i(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y);
    }
    static Vector2i Max(const Vector2i& a, const Vector2i& b) {
        return Vector2i(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y);
    }
};

} // namespace bighero
