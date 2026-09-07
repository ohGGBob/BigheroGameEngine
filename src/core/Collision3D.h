#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// 3D collision primitives (discrete). Pure standard library, self-contained.
class Collision3D {
public:
    // Sphere vs sphere.
    static bool SphereSphere(float ax, float ay, float az, float ar,
                             float bx, float by, float bz, float br) {
        float dx = bx - ax, dy = by - ay, dz = bz - az;
        float rs = ar + br;
        return (dx * dx + dy * dy + dz * dz) <= rs * rs;
    }

    // AABB (min/max) vs AABB overlap.
    static bool AabbAabb(float aminx, float aminy, float aminz,
                         float amaxx, float amaxy, float amaxz,
                         float bminx, float bminy, float bminz,
                         float bmaxx, float bmaxy, float bmaxz) {
        return !(bminx > amaxx || bmaxx < aminx ||
                 bminy > amaxy || bmaxy < aminy ||
                 bminz > amaxz || bmaxz < aminz);
    }

    // Sphere vs AABB (closest point).
    static bool SphereAabb(float scx, float scy, float scz, float sr,
                           float aminx, float aminy, float aminz,
                           float amaxx, float amaxy, float amaxz) {
        float nx = std::max(aminx, std::min(scx, amaxx));
        float ny = std::max(aminy, std::min(scy, amaxy));
        float nz = std::max(aminz, std::min(scz, amaxz));
        float dx = scx - nx, dy = scy - ny, dz = scz - nz;
        return (dx * dx + dy * dy + dz * dz) <= sr * sr;
    }

    // Ray (origin o, dir d) vs sphere, Möller. Writes near t.
    static bool RaySphere(float ox, float oy, float oz,
                          float dx, float dy, float dz,
                          float cx, float cy, float cz, float r,
                          float& tNear) {
        float lx = cx - ox, ly = cy - oy, lz = cz - oz;
        float tca = lx * dx + ly * dy + lz * dz;
        float d2 = (lx * lx + ly * ly + lz * lz) - tca * tca;
        float r2 = r * r;
        if (d2 > r2) return false;
        float thc = std::sqrt(r2 - d2);
        tNear = tca - thc;
        return tNear > 0.0f;
    }
};

} // namespace bighero
