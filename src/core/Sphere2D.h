#pragma once
#include <cmath>

namespace bighero {

// Sphere2D: a 2D circle with area/circumference, containment, and
// circle-circle overlap queries. Self-contained, std-lib only.
struct Sphere2D {
    float cx = 0, cy = 0;
    float radius = 1.0f;

    Sphere2D() = default;
    Sphere2D(float cx_, float cy_, float r_) : cx(cx_), cy(cy_), radius(r_) {}

    bool Contains(float px, float py) const {
        float dx = px - cx, dy = py - cy;
        return (dx * dx + dy * dy) <= radius * radius;
    }
    bool Overlaps(const Sphere2D& o) const {
        float dx = cx - o.cx, dy = cy - o.cy;
        float rr = radius + o.radius;
        return (dx * dx + dy * dy) <= rr * rr;
    }
    float Area() const { return 3.14159265f * radius * radius; }
    float Circumference() const { return 2 * 3.14159265f * radius; }
    void Bounds(float& xmin, float& ymin, float& xmax, float& ymax) const {
        xmin = cx - radius; ymin = cy - radius;
        xmax = cx + radius; ymax = cy + radius;
    }
};

} // namespace bighero
