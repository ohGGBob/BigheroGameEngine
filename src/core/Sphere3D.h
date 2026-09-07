#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// 3D sphere.
struct Sphere3D {
    float cx, cy, cz, radius;

    Sphere3D() : cx(0), cy(0), cz(0), radius(0) {}

    Sphere3D(float cx_, float cy_, float cz_, float r)
        : cx(cx_), cy(cy_), cz(cz_), radius(r) {}

    bool Contains(float px, float py, float pz) const {
        float dx = px - cx, dy = py - cy, dz = pz - cz;
        return (dx * dx + dy * dy + dz * dz) <= radius * radius;
    }

    bool Intersects(const Sphere3D& o) const {
        float dx = cx - o.cx, dy = cy - o.cy, dz = cz - o.cz;
        float rs = radius + o.radius;
        return (dx * dx + dy * dy + dz * dz) <= rs * rs;
    }

    float Volume() const {
        return (4.0f / 3.0f) * 3.14159265f * radius * radius * radius;
    }
};

} // namespace bighero
