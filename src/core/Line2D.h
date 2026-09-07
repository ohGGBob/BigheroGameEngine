#pragma once
#include <cmath>

namespace bighero {

// 2D infinite line (point + direction) and segment helpers.
struct Line2D {
    float ox, oy; // origin
    float dx, dy; // direction (not necessarily normalized)

    Line2D() : ox(0), oy(0), dx(1), dy(0) {}
    Line2D(float px, float py, float vx, float vy) : ox(px), oy(py), dx(vx), dy(vy) {}

    void SetDirection(float vx, float vy) { dx = vx; dy = vy; }

    // Closest point on the infinite line to (px,py).
    void ClosestPoint(float px, float py, float& cx, float& cy) const {
        float len2 = dx*dx + dy*dy;
        if (len2 <= 0) { cx = ox; cy = oy; return; }
        float t = ((px - ox)*dx + (py - oy)*dy) / len2;
        cx = ox + t*dx; cy = oy + t*dy;
    }

    // Distance from point to the infinite line.
    float DistanceTo(float px, float py) const {
        float cx, cy;
        ClosestPoint(px, py, cx, cy);
        float ex = px - cx, ey = py - cy;
        return std::sqrt(ex*ex + ey*ey);
    }

    // Project a point onto the line, returning scalar t (in direction units).
    float ProjectT(float px, float py) const {
        float len2 = dx*dx + dy*dy;
        if (len2 <= 0) return 0;
        return ((px - ox)*dx + (py - oy)*dy) / len2;
    }

    // Intersection of two infinite lines; returns false if parallel.
    static bool Intersect(const Line2D& a, const Line2D& b, float& x, float& y) {
        float cross = a.dx*b.dy - a.dy*b.dx;
        if (std::fabs(cross) < 1e-8f) return false;
        float t = ((b.ox - a.ox)*b.dy - (b.oy - a.oy)*b.dx) / cross;
        x = a.ox + t*a.dx;
        y = a.oy + t*a.dy;
        return true;
    }
};

} // namespace bighero
