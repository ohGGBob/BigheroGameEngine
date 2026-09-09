#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// BoxCollider: axis-aligned box collider descriptor (center + half-extents).
// Pure CPU-side data + containment/overlap helpers, self-contained.
class BoxCollider {
public:
    BoxCollider() {}
    BoxCollider(float cx, float cy, float cz, float hx, float hy, float hz)
        : cx_(cx), cy_(cy), cz_(cz), hx_(hx), hy_(hy), hz_(hz) {}

    void SetCenter(float x, float y, float z) { cx_=x; cy_=y; cz_=z; }
    void Center(float& x, float& y, float& z) const { x=cx_; y=cy_; z=cz_; }
    void SetHalfExtents(float x, float y, float z) { hx_=x; hy_=y; hz_=z; }
    void HalfExtents(float& x, float& y, float& z) const { x=hx_; y=hy_; z=hz_; }

    bool Contains(float x, float y, float z) const {
        return std::fabs(x-cx_)<=hx_ && std::fabs(y-cy_)<=hy_ && std::fabs(z-cz_)<=hz_;
    }
    // Overlap test against another box collider (AABB).
    bool Overlaps(const BoxCollider& o) const {
        return std::fabs(cx_-o.cx_) <= hx_+o.hx_ &&
               std::fabs(cy_-o.cy_) <= hy_+o.hy_ &&
               std::fabs(cz_-o.cz_) <= hz_+o.hz_;
    }
    void SetTrigger(bool t) { trigger_ = t; }
    bool IsTrigger() const { return trigger_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    void SetBounds(float minX,float minY,float minZ,float maxX,float maxY,float maxZ) {
        cx_=(minX+maxX)*0.5f; cy_=(minY+maxY)*0.5f; cz_=(minZ+maxZ)*0.5f;
        hx_=(maxX-minX)*0.5f; hy_=(maxY-minY)*0.5f; hz_=(maxZ-minZ)*0.5f;
    }
    void Bounds(float& minX,float& minY,float& minZ,float& maxX,float& maxY,float& maxZ) const {
        minX=cx_-hx_; minY=cy_-hy_; minZ=cz_-hz_;
        maxX=cx_+hx_; maxY=cy_+hy_; maxZ=cz_+hz_;
    }

private:
    float cx_=0, cy_=0, cz_=0;
    float hx_=0.5f, hy_=0.5f, hz_=0.5f;
    bool trigger_=false, enabled_=true;
};

} // namespace bighero
