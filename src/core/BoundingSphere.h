#pragma once
#include <cmath>

namespace bighero {

// Bounding sphere: center + radius, for broad-phase culling.
struct BoundingSphere {
    float cx, cy, cz;
    float radius;

    BoundingSphere() : cx(0), cy(0), cz(0), radius(0) {}

    BoundingSphere(float cx_, float cy_, float cz_, float r) : cx(cx_), cy(cy_), cz(cz_), radius(r) {}

    bool Contains(const float px, const float py, const float pz) const {
        float dx = px - cx, dy = py - cy, dz = pz - cz;
        return (dx * dx + dy * dy + dz * dz) <= radius * radius;
    }

    bool Intersects(const BoundingSphere& o) const {
        float dx = cx - o.cx, dy = cy - o.cy, dz = cz - o.cz;
        float rsum = radius + o.radius;
        return (dx * dx + dy * dy + dz * dz) <= rsum * rsum;
    }

    void ExpandToInclude(const float px, const float py, const float pz) {
        float dx = px - cx, dy = py - cy, dz = pz - cz;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz) + radius;
        if (dist > radius) {
            // Grow radius so the sphere covers the new point from its center.
            float newR = dist * 0.5f;
            // Re-center toward the point.
            float t = (dist - radius) * 0.5f / (dist + 1e-8f);
            cx += dx * t; cy += dy * t; cz += dz * t;
            radius = newR;
        }
    }

    // Fast sphere vs AABB test. Cheap but conservative.
    bool IntersectsAabb(const float minx, const float miny, const float minz,
                        const float maxx, const float maxy, const float maxz) const {
        float nearestX = std::max(minx, std::min(cx, maxx));
        float nearestY = std::max(miny, std::min(cy, maxy));
        float nearestZ = std::max(minz, std::min(cz, maxz));
        float dx = cx - nearestX, dy = cy - nearestY, dz = cz - nearestZ;
        return (dx * dx + dy * dy + dz * dz) <= radius * radius;
    }
};

} // namespace bighero
