#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// 3D signed distance field primitives. Each returns signed distance to a shape
// (negative inside, positive outside). Pure standard library.
class Sdf3D {
public:
    static float Sphere(float px, float py, float pz, float cx, float cy, float cz, float r) {
        float dx = px - cx, dy = py - cy, dz = pz - cz;
        return std::sqrt(dx * dx + dy * dy + dz * dz) - r;
    }

    static float Box(float px, float py, float pz, float cx, float cy, float cz,
                     float hx, float hy, float hz) {
        float dx = std::fabs(px - cx) - hx;
        float dy = std::fabs(py - cy) - hy;
        float dz = std::fabs(pz - cz) - hz;
        float ox = std::max(dx, 0.0f), oy = std::max(dy, 0.0f), oz = std::max(dz, 0.0f);
        return std::sqrt(ox * ox + oy * oy + oz * oz)
             + std::min(std::max(dx, std::max(dy, dz)), 0.0f);
    }

    static float Plane(float px, float py, float pz, float nx, float ny, float nz, float offset) {
        return (nx * px + ny * py + nz * pz + offset);
    }

    static float Torus(float px, float py, float pz, float cx, float cy, float cz,
                       float majorR, float minorR) {
        float dx = px - cx, dy = py - cy, dz = pz - cz;
        float q = std::sqrt(dx * dx + dz * dz) - majorR;
        return std::sqrt(q * q + dy * dy) - minorR;
    }

    static float Union(float a, float b) { return std::min(a, b); }
    static float Intersect(float a, float b) { return std::max(a, b); }
    static float Subtract(float a, float b) { return std::max(a, -b); }
};

} // namespace bighero
