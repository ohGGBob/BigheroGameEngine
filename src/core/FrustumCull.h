#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

struct FrustumPlane {
    float nx, ny, nz, d;
    float Distance(const float px, const float py, const float pz) const {
        return nx * px + ny * py + nz * pz + d;
    }
    bool InFront(const float px, const float py, const float pz) const {
        return Distance(px, py, pz) >= 0.0f;
    }
};

// Bounding frustum defined by 6 planes (left/right/top/bottom/near/far),
// each with outward-pointing normals. Point/aabb/sphere culling tests.
class FrustumCull {
public:
    FrustumPlane planes[6];

    void Set(int i, float nx, float ny, float nz, float d) {
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len < 1e-8f) len = 1.0f;
        planes[i].nx = nx / len; planes[i].ny = ny / len; planes[i].nz = nz / len;
        planes[i].d = d / len;
    }

    bool ContainsPoint(const float px, const float py, const float pz) const {
        for (int i = 0; i < 6; ++i)
            if (!planes[i].InFront(px, py, pz)) return false;
        return true;
    }

    // Sphere is fully inside if center is inside and distance-to-plane >= radius for all planes.
    bool ContainsSphere(const float cx, const float cy, const float cz, float radius) const {
        for (int i = 0; i < 6; ++i) {
            float dist = planes[i].Distance(cx, cy, cz);
            if (dist < -radius) return false; // fully outside
        }
        return true;
    }

    bool IntersectsSphere(const float cx, const float cy, const float cz, float radius) const {
        for (int i = 0; i < 6; ++i) {
            float dist = planes[i].Distance(cx, cy, cz);
            if (dist < -radius) return false;
        }
        return true;
    }

    // AABB: test each plane; if the box is fully outside any plane -> not visible.
    bool IntersectsAabb(const float minx, const float miny, const float minz,
                        const float maxx, const float maxy, const float maxz) const {
        for (int i = 0; i < 6; ++i) {
            const FrustumPlane& p = planes[i];
            // Find positive (far) vertex along the plane normal.
            float vx = (p.nx >= 0.0f) ? maxx : minx;
            float vy = (p.ny >= 0.0f) ? maxy : miny;
            float vz = (p.nz >= 0.0f) ? maxz : minz;
            if (p.Distance(vx, vy, vz) < 0.0f) return false;
        }
        return true;
    }

    bool ContainsAabb(const float minx, const float miny, const float minz,
                      const float maxx, const float maxy, const float maxz) const {
        // Negative (near) vertex must be inside all planes.
        for (int i = 0; i < 6; ++i) {
            const FrustumPlane& p = planes[i];
            float vx = (p.nx >= 0.0f) ? minx : maxx;
            float vy = (p.ny >= 0.0f) ? miny : maxy;
            float vz = (p.nz >= 0.0f) ? minz : maxz;
            if (!p.InFront(vx, vy, vz)) return false;
        }
        return true;
    }
};

} // namespace bighero
