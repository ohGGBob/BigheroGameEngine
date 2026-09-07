#pragma once

namespace bighero {

// Physical material properties for collision/response.
struct PhysicsMaterial {
    float restitution = 0.2f;   // bounciness (0 = inelastic, ~1 = elastic)
    float friction = 0.5f;      // Coulomb friction coefficient
    float density = 1.0f;       // mass per unit area/volume
    float stiffness = 10000.0f; // scratch spring stiffness (for soft bodies)

    void SetRestitution(float r) { restitution = r < 0 ? 0 : (r > 1 ? 1 : r); }
    void SetFriction(float f)   { friction = f < 0 ? 0 : f; }
    void SetDensity(float d)    { density = d > 0 ? d : 1.0f; }
    void SetStiffness(float s)  { stiffness = s > 0 ? s : 1.0f; }

    // Combine two materials for a contact (averaging used in many engines).
    static PhysicsMaterial Combine(const PhysicsMaterial& a, const PhysicsMaterial& b) {
        PhysicsMaterial m;
        m.restitution = a.restitution * b.restitution; // product for bounciness
        m.friction = a.friction * b.friction;
        m.density = (a.density + b.density) * 0.5f;
        m.stiffness = (a.stiffness + b.stiffness) * 0.5f;
        return m;
    }
};

} // namespace bighero
