#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// CapsuleCollider: capsule collider descriptor (center, top/bottom ends along
// one axis, and radius). Pure CPU-side data + containment/overlap helpers.
class CapsuleCollider {
public:
    enum class Axis { X, Y, Z };

    CapsuleCollider() {}
    CapsuleCollider(float cx, float cy, float cz, float halfHeight, float r)
        : cx_(cx), cy_(cy), cz_(cz), hh_(halfHeight), r_(r) {}

    void SetCenter(float x, float y, float z) { cx_=x; cy_=y; cz_=z; }
    void Center(float& x, float& y, float& z) const { x=cx_; y=cy_; z=cz_; }
    void SetHalfHeight(float h) { hh_ = h < 0 ? 0 : h; }
    float HalfHeight() const { return hh_; }
    void SetRadius(float r) { r_ = r < 0 ? 0 : r; }
    float Radius() const { return r_; }
    void SetAxis(Axis a) { axis_ = a; }
    Axis CurrentAxis() const { return axis_; }

    // Distance from center to capsule surface along the axis (signed).
    float TopExtent() const { return hh_; }
    float BottomExtent() const { return -hh_; }

    bool Contains(float x, float y, float z) const {
        float dx=x-cx_, dy=y-cy_, dz=z-cz_;
        // Along chosen axis clamp to segment, then test radial distance.
        float along;
        if (axis_ == Axis::X) along = clamp(-hh_, hh_, dx);
        else if (axis_ == Axis::Y) along = clamp(-hh_, hh_, dy);
        else along = clamp(-hh_, hh_, dz);
        float axC = dx - (axis_ == Axis::X ? along : 0);
        float ayC = dy - (axis_ == Axis::Y ? along : 0);
        float azC = dz - (axis_ == Axis::Z ? along : 0);
        return axC*axC+ayC*ayC+azC*azC <= r_*r_;
    }

    void SetTrigger(bool t) { trigger_ = t; }
    bool IsTrigger() const { return trigger_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    static float clamp(float lo, float hi, float v) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    float cx_=0, cy_=0, cz_=0, hh_=1.0f, r_=0.5f;
    Axis axis_ = Axis::Y;
    bool trigger_=false, enabled_=true;
};

} // namespace bighero
