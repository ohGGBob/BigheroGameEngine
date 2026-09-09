#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// PhysicsJoint: a generic joint connecting two bodies. Holds type, the two
// body ids, and a small set of config params (anchor offsets, distance/limits).
// Pure CPU-side descriptor; solver supplied by physics backend.
class PhysicsJoint {
public:
    enum class Type { None, Distance, Hinge, Slider, BallSocket, Fixed };

    PhysicsJoint() {}
    PhysicsJoint(Type t, std::size_t a, std::size_t b)
        : type_(t), bodyA_(a), bodyB_(b) {}

    void SetType(Type t) { type_ = t; }
    Type Current() const { return type_; }
    void SetBodies(std::size_t a, std::size_t b) { bodyA_=a; bodyB_=b; }
    std::size_t BodyA() const { return bodyA_; }
    std::size_t BodyB() const { return bodyB_; }

    void SetAnchorA(float x, float y, float z) { ax_=x; ay_=y; az_=z; }
    void AnchorA(float& x, float& y, float& z) const { x=ax_; y=ay_; z=az_; }
    void SetAnchorB(float x, float y, float z) { bx_=x; by_=y; bz_=z; }
    void AnchorB(float& x, float& y, float& z) const { x=bx_; y=by_; z=bz_; }

    void SetMinDistance(float d) { min_ = d; }
    float MinDistance() const { return min_; }
    void SetMaxDistance(float d) { max_ = d; }
    float MaxDistance() const { return max_; }
    void SetBreakForce(float f) { breakForce_ = f; }
    float BreakForce() const { return breakForce_; }
    void SetEnableCollision(bool c) { collision_ = c; }
    bool EnableCollision() const { return collision_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    void SetSoftness(float s) { softness_ = s < 0 ? 0 : (s > 1 ? 1 : s); }
    float Softness() const { return softness_; }

private:
    Type type_ = Type::None;
    std::size_t bodyA_=0, bodyB_=0;
    float ax_=0, ay_=0, az_=0, bx_=0, by_=0, bz_=0;
    float min_=0, max_=0, breakForce_=0, softness_=0;
    bool collision_=false, enabled_=true;
};

} // namespace bighero
