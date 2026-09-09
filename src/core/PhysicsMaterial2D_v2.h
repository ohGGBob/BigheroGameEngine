#pragma once
#include <cstdint>

namespace bighero {

// PhysicsMaterial2D: friction and bounciness material used by 2D colliders.
// Self-contained, std-lib only.
class PhysicsMaterial2D {
public:
    PhysicsMaterial2D() = default;
    PhysicsMaterial2D(float friction, float restitution)
        : friction_(friction), restitution_(restitution) {}

    void SetFriction(float f) { friction_ = f < 0 ? 0 : f; }
    float Friction() const { return friction_; }

    void SetRestitution(float r) { restitution_ = r < 0 ? 0 : (r > 1 ? 1 : r); }
    float Restitution() const { return restitution_; }

    void SetDensity(float d) { density_ = d < 0 ? 0 : d; }
    float Density() const { return density_; }

    // Combined friction (geometric mean) between two materials.
    static float CombineFriction(const PhysicsMaterial2D& a, const PhysicsMaterial2D& b) {
        return a.friction_ * b.friction_;
    }
    // Combined restitution (max) between two materials.
    static float CombineRestitution(const PhysicsMaterial2D& a, const PhysicsMaterial2D& b) {
        return a.restitution_ > b.restitution_ ? a.restitution_ : b.restitution_;
    }

private:
    float friction_ = 0.4f;
    float restitution_ = 0.0f;
    float density_ = 1.0f;
};

} // namespace bighero
