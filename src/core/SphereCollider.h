#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// SphereCollider: sphere collider descriptor (center + radius).
// Pure CPU-side data + containment/overlap helpers, self-contained.
class SphereCollider {
public:
    SphereCollider() {}
    SphereCollider(float cx, float cy, float cz, float r)
        : cx_(cx), cy_(cy), cz_(cz), r_(r) {}

    void SetCenter(float x, float y, float z) { cx_=x; cy_=y; cz_=z; }
    void Center(float& x, float& y, float& z) const { x=cx_; y=cy_; z=cz_; }
    void SetRadius(float r) { r_ = r < 0 ? 0 : r; }
    float Radius() const { return r_; }

    bool Contains(float x, float y, float z) const {
        float dx=x-cx_, dy=y-cy_, dz=z-cz_;
        return dx*dx+dy*dy+dz*dz <= r_*r_;
    }
    bool Overlaps(const SphereCollider& o) const {
        float dx=cx_-o.cx_, dy=cy_-o.cy_, dz=cz_-o.cz_;
        float rr = r_ + o.r_;
        return dx*dx+dy*dy+dz*dz <= rr*rr;
    }
    void SetTrigger(bool t) { trigger_ = t; }
    bool IsTrigger() const { return trigger_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    float cx_=0, cy_=0, cz_=0, r_=0.5f;
    bool trigger_=false, enabled_=true;
};

} // namespace bighero
