#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// Capsule: line segment (a-b) with radius. Useful for character collision.
struct Capsule3D {
    float ax, ay, az;
    float bx, by, bz;
    float radius;

    Capsule3D() : ax(0), ay(0), az(0), bx(0), by(1), bz(0), radius(0.5f) {}

    Capsule3D(float ax_, float ay_, float az_, float bx_, float by_, float bz_, float r)
        : ax(ax_), ay(ay_), az(az_), bx(bx_), by(by_), bz(bz_), radius(r) {}

    // Distance from a point to the segment axis; if <= radius the point is inside.
    float DistanceToPoint(float px, float py, float pz) const {
        float abx = bx - ax, aby = by - ay, abz = bz - az;
        float apx = px - ax, apy = py - ay, apz = pz - az;
        float abLenSq = abx * abx + aby * aby + abz * abz;
        float t = 0.0f;
        if (abLenSq > 1e-10f) {
            t = (apx * abx + apy * aby + apz * abz) / abLenSq;
            t = std::max(0.0f, std::min(1.0f, t));
        }
        float cx = ax + abx * t, cy = ay + aby * t, cz = az + abz * t;
        float dx = px - cx, dy = py - cy, dz = pz - cz;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    bool Contains(float px, float py, float pz) const {
        return DistanceToPoint(px, py, pz) <= radius;
    }
};

} // namespace bighero
