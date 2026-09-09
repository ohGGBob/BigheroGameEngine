#pragma once
#include <cmath>

namespace bighero {

// Infinite plane defined by point + normal. Point-normal form.
struct Plane {
    float nx, ny, nz; // normal
    float d;          // offset: d = -dot(normal, point)

    Plane() : nx(0), ny(1), nz(0), d(0) {}

    Plane(float nx_, float ny_, float nz_, float d_) : nx(nx_), ny(ny_), nz(nz_), d(d_) {}

    // Build from a normal and a point on the plane.
    static Plane FromPointNormal(const float px, const float py, const float pz,
                                 const float nx_, const float ny_, const float nz_) {
        float len = std::sqrt(nx_ * nx_ + ny_ * ny_ + nz_ * nz_);
        if (len < 1e-8f) len = 1.0f;
        float ux = nx_ / len, uy = ny_ / len, uz = nz_ / len;
        float dd = -(ux * px + uy * py + uz * pz);
        return Plane(ux, uy, uz, dd);
    }

    float Distance(const float px, const float py, const float pz) const {
        return nx * px + ny * py + nz * pz + d;
    }

    // Returns >0 if point is in front (normal side), <0 behind, 0 on plane.
    float SignedDistance(const float px, const float py, const float pz) const {
        return Distance(px, py, pz);
    }

    bool IsInFront(const float px, const float py, const float pz) const {
        return Distance(px, py, pz) > 0.0f;
    }

    void Normalize() {
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len < 1e-8f) return;
        nx /= len; ny /= len; nz /= len; d /= len;
    }
};

} // namespace bighero
