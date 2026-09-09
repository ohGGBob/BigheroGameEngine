#pragma once
#include <cmath>

namespace bighero {

// Capsule: a 2D capsule shape (a segment swept by a radius) with point
// containment, distance, and closest-point queries. Self-contained.
struct Capsule {
    float ax = 0, ay = 0;   // segment endpoint A
    float bx = 0, by = 0;   // segment endpoint B
    float radius = 0.5f;

    Capsule() = default;
    Capsule(float ax_, float ay_, float bx_, float by_, float r_)
        : ax(ax_), ay(ay_), bx(bx_), by(by_), radius(r_) {}

    // Closest point on the segment AB to point P.
    void ClosestPoint(float px, float py, float& ox, float& oy) const {
        float dx = bx - ax, dy = by - ay;
        float len2 = dx * dx + dy * dy;
        if (len2 < 1e-12f) { ox = ax; oy = ay; return; }
        float t = ((px - ax) * dx + (py - ay) * dy) / len2;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        ox = ax + t * dx; oy = ay + t * dy;
    }
    // Distance from point P to the capsule surface.
    float Distance(float px, float py) const {
        float cx, cy;
        ClosestPoint(px, py, cx, cy);
        float d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
        return d > radius ? d - radius : 0.0f;
    }
    bool Contains(float px, float py) const { return Distance(px, py) <= 0.0f; }
    float Length() const { return std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay)); }
};

} // namespace bighero
