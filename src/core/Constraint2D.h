#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// Constraint2D: a 2D distance/angular constraint between two points, used by
// the 2D physics solver. Holds local anchors, rest length, and stiffness.
// Pure CPU-side descriptor + small helpers.
class Constraint2D {
public:
    enum class Type { Distance, Revolute, Prismatic };

    Constraint2D() {}
    explicit Constraint2D(Type t) : type_(t) {}

    void SetType(Type t) { type_ = t; }
    Type Current() const { return type_; }
    void SetLocalAnchorA(float x, float y) { ax_=x; ay_=y; }
    void SetLocalAnchorB(float x, float y) { bx_=x; by_=y; }
    void AnchorA(float& x, float& y) const { x=ax_; y=ay_; }
    void AnchorB(float& x, float& y) const { x=bx_; y=by_; }
    void SetRestLength(float len) { rest_ = len < 0 ? 0 : len; }
    float RestLength() const { return rest_; }
    void SetStiffness(float k) { stiffness_ = k < 0 ? 0 : k; }
    float Stiffness() const { return stiffness_; }
    void SetDamping(float d) { damping_ = d < 0 ? 0 : (d > 1 ? 1 : d); }
    float Damping() const { return damping_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    // Compute scalar separation between the two anchors (world positions).
    float Separation(float axW, float ayW, float bxW, float byW) const {
        float dx = bxW - axW, dy = byW - ayW;
        return std::sqrt(dx*dx + dy*dy);
    }
    // Correction needed to reach rest length (positive = stretch).
    float Error(float axW, float ayW, float bxW, float byW) const {
        return Separation(axW, ayW, bxW, byW) - rest_;
    }

private:
    Type type_ = Type::Distance;
    float ax_=0, ay_=0, bx_=0, by_=0;
    float rest_=0, stiffness_=0.5f, damping_=0.0f;
    bool enabled_=true;
};

} // namespace bighero
