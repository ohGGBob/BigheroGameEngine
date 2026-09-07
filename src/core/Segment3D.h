#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// 3D line segment between two endpoints.
struct Segment3D {
    float ax, ay, az, bx, by, bz;

    Segment3D() : ax(0), ay(0), az(0), bx(1), by(0), bz(0) {}
    Segment3D(float ax_, float ay_, float az_, float bx_, float by_, float bz_)
        : ax(ax_), ay(ay_), az(az_), bx(bx_), by(by_), bz(bz_) {}

    float Length() const { return std::sqrt((bx-ax)*(bx-ax)+(by-ay)*(by-ay)+(bz-az)*(bz-az)); }
    float LengthSq() const { return (bx-ax)*(bx-ax)+(by-ay)*(by-ay)+(bz-az)*(bz-az); }

    void ClosestPoint(float px, float py, float pz, float& ox, float& oy, float& oz) const {
        float vx = bx - ax, vy = by - ay, vz = bz - az;
        float lenSq = vx*vx + vy*vy + vz*vz;
        float t = 0.0f;
        if (lenSq > 1e-10f) {
            t = ((px-ax)*vx + (py-ay)*vy + (pz-az)*vz) / lenSq;
            t = std::max(0.0f, std::min(1.0f, t));
        }
        ox = ax + vx*t; oy = ay + vy*t; oz = az + vz*t;
    }

    float DistanceToPoint(float px, float py, float pz) const {
        float cx, cy, cz; ClosestPoint(px, py, pz, cx, cy, cz);
        return std::sqrt((px-cx)*(px-cx) + (py-cy)*(py-cy) + (pz-cz)*(pz-cz));
    }
};

} // namespace bighero
