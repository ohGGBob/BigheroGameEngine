#pragma once
#include <cmath>

namespace bighero {

// Critically/base damped spring for smooth target-following dynamics.
// Common for camera follow, UI ease, and mass-spring animation.
class SpringDamper {
public:
    SpringDamper(float stiffness = 40.0f, float damping = 8.0f)
        : stiffness_(stiffness), damping_(damping) {}

    // Advance toward `target` by dt, updating current value and velocity.
    void Update(float dt, float target) {
        // Semi-implicit Euler for the spring ODE.
        float a = stiffness_ * (target - value_) - damping_ * velocity_;
        velocity_ += a * dt;
        value_ += velocity_ * dt;
    }

    void SetPosition(float v) { value_ = v; }
    void SetVelocity(float v) { velocity_ = v; }
    void Reset(float v = 0) { value_ = v; velocity_ = 0; }
    void SetParams(float stiffness, float damping) { stiffness_ = stiffness; damping_ = damping; }

    float Value() const { return value_; }
    float Velocity() const { return velocity_; }
    bool IsSettled(float target, float eps = 0.001f) const {
        return std::fabs(value_ - target) < eps && std::fabs(velocity_) < eps;
    }

private:
    float stiffness_;
    float damping_;
    float value_ = 0;
    float velocity_ = 0;
};

} // namespace bighero
