#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// OrientedBox: an oriented bounding box (center + half-extents + a 2D or 3D
// orientation) used for physics broad/narrow-phase and culling. Pure
// CPU-side, self-contained.
class OrientedBox {
public:
    OrientedBox() {}

    OrientedBox(float cx, float cy, float cz, float hx, float hy, float hz)
        : cx_(cx), cy_(cy), cz_(cz), hx_(hx), hy_(hy), hz_(hz) {}

    void SetCenter(float cx, float cy, float cz) { cx_=cx; cy_=cy; cz_=cz; }
    void Center(float& cx, float& cy, float& cz) const { cx=cx_; cy=cy_; cz=cz_; }
    void SetHalfExtents(float hx, float hy, float hz) { hx_=hx; hy_=hy; hz_=hz; }
    void HalfExtents(float& hx, float& hy, float& hz) const { hx=hx_; hy=hy_; hz=hz_; }

    // Simple yaw rotation (around Z axis) in radians.
    void SetYaw(float rad) { yaw_ = rad; }
    float Yaw() const { return yaw_; }
    void SetRotation(float yaw, float pitch, float roll) { yaw_=yaw; pitch_=pitch; roll_=roll; }

    // Conservative AABB of the oriented box.
    void AABB(float& minX, float& minY, float& minZ,
              float& maxX, float& maxY, float& maxZ) const {
        float cy = std::cos(yaw_), sy = std::sin(yaw_);
        float exX = hx_*std::fabs(cy) + hy_*std::fabs(sy);
        float exY = hx_*std::fabs(sy) + hy_*std::fabs(cy);
        minX = cx_-exX; maxX = cx_+exX;
        minY = cy_-exY; maxY = cy_+exY;
        minZ = cz_-hz_;  maxZ = cz_+hz_;
    }

    // Contains a world point (ignores pitch/roll; uses yaw rotation).
    bool Contains(float px, float py, float pz) const {
        float dx = px - cx_, dy = py - cy_;
        float cy = std::cos(yaw_), sy = std::sin(yaw_);
        float lx =  cy*dx + sy*dy;
        float ly = -sy*dx + cy*dy;
        float lz = pz - cz_;
        return std::fabs(lx) <= hx_ && std::fabs(ly) <= hy_ && std::fabs(lz) <= hz_;
    }

    float Volume() const { return 8.0f*hx_*hy_*hz_; }

private:
    float cx_=0, cy_=0, cz_=0;
    float hx_=1, hy_=1, hz_=1;
    float yaw_=0, pitch_=0, roll_=0;
};

} // namespace bighero
