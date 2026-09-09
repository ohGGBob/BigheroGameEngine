#pragma once
#include <cstdint>

namespace bighero {

// Friction: a Coulomb-friction helper that computes the friction force
// magnitude and whether static friction holds. Self-contained, std-lib only.
class Friction {
public:
    Friction() = default;
    Friction(float staticMu, float kineticMu) : staticMu_(staticMu), kineticMu_(kineticMu) {}

    void SetCoefficients(float staticMu, float kineticMu) {
        staticMu_ = staticMu < 0 ? 0 : staticMu;
        kineticMu_ = kineticMu < 0 ? 0 : kineticMu;
    }
    float StaticCoefficient() const { return staticMu_; }
    float KineticCoefficient() const { return kineticMu_; }

    // Maximum static friction magnitude for a given normal force.
    float MaxStatic(float normalForce) const {
        return normalForce > 0 ? staticMu_ * normalForce : 0;
    }
    // Kinetic friction magnitude for a given normal force.
    float Kinetic(float normalForce) const {
        return normalForce > 0 ? kineticMu_ * normalForce : 0;
    }

    // Compute the friction force that resists a tangential load 'load'.
    // If load <= maxStatic, static friction holds (returns load, stuck=true);
    // otherwise kinetic friction applies (returns kinetic, stuck=false).
    float Resolve(float normalForce, float load, bool& stuck) const {
        if (normalForce <= 0) { stuck = (load == 0); return 0; }
        float maxStatic = MaxStatic(normalForce);
        float absLoad = load < 0 ? -load : load;
        if (absLoad <= maxStatic) {
            stuck = true;
            return load;
        }
        stuck = false;
        float k = Kinetic(normalForce);
        return load < 0 ? -k : k;
    }

    // Is the object at rest given applied load and static friction limit?
    bool IsStaticHeld(float normalForce, float load) const {
        if (normalForce <= 0) return load == 0;
        float absLoad = load < 0 ? -load : load;
        return absLoad <= MaxStatic(normalForce);
    }

private:
    float staticMu_ = 0.4f;
    float kineticMu_ = 0.3f;
};

} // namespace bighero
