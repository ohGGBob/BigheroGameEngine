#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// SpringConstraint: a damped-spring constraint between two bodies used by the
// physics solver to pull them toward a rest distance. Pure CPU-side,
// standard-library only, self-contained.
class SpringConstraint {
public:
    SpringConstraint() {}
    SpringConstraint(std::size_t bodyA, std::size_t bodyB, float restLength)
        : bodyA_(bodyA), bodyB_(bodyB), rest_(restLength) {}

    void SetBodies(std::size_t a, std::size_t b) { bodyA_=a; bodyB_=b; }
    std::size_t BodyA() const { return bodyA_; }
    std::size_t BodyB() const { return bodyB_; }

    void SetRestLength(float r) { rest_ = r; }
    float RestLength() const { return rest_; }

    void SetStiffness(float k) { stiffness_ = k < 0 ? 0 : k; }
    float Stiffness() const { return stiffness_; }
    void SetDamping(float c) { damping_ = c < 0 ? 0 : c; }
    float Damping() const { return damping_; }

    // Compute the spring force magnitude along the axis given current length
    // and relative velocity along that axis. Hooke + damper.
    float Force(float currentLength, float relativeVel) const {
        float f = -stiffness_ * (currentLength - rest_) - damping_ * relativeVel;
        return f;
    }

    bool active() const { return active_; }
    void SetActive(bool a) { active_ = a; }

private:
    std::size_t bodyA_=0, bodyB_=0;
    float rest_=0.0f, stiffness_=10.0f, damping_=0.5f;
    bool active_=true;
};

} // namespace bighero
