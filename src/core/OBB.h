#pragma once
#include <cmath>

namespace bighero {

// OBB: an oriented bounding box in 2D (center + half-extents + rotation).
// Provides local-space transform, containment, and approximate radius helpers.
struct OBB {
    float cx = 0, cy = 0;   // center
    float hx = 1, hy = 1;   // half extents
    float angle = 0;        // rotation in radians

    OBB() = default;
    OBB(float cx_, float cy_, float hx_, float hy_, float angle_ = 0)
        : cx(cx_), cy(cy_), hx(hx_), hy(hy_), angle(angle_) {}

    // Transform a world point into local OBB space and test containment.
    bool Contains(float px, float py) const {
        float dx = px - cx, dy = py - cy;
        float c = std::cos(-angle), s = std::sin(-angle);
        float lx = dx * c - dy * s;
        float ly = dx * s + dy * c;
        return std::fabs(lx) <= hx && std::fabs(ly) <= hy;
    }
    // Radius of the bounding circle that encloses the OBB.
    float BoundingRadius() const { return std::sqrt(hx * hx + hy * hy); }
    void Corners(float out[8]) const {
        float c = std::cos(angle), s = std::sin(angle);
        float exX = c * hx, exY = s * hx;   // x-axis extents
        float eyX = -s * hy, eyY = c * hy;  // y-axis extents
        out[0] = cx - exX - eyX; out[1] = cy - exY - eyY;
        out[2] = cx + exX - eyX; out[3] = cy + exY - eyY;
        out[4] = cx + exX + eyX; out[5] = cy + exY + eyY;
        out[6] = cx - exX + eyX; out[7] = cy - exY + eyY;
    }
};

} // namespace bighero
