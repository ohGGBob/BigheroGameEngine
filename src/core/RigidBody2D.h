#pragma once
#include <vector>
#include <cmath>
#include <cstddef>

namespace bighero {

// Minimal 2D rigid body: position, velocity, mass, gravity scale, and a
// linear/angular damping model. Building block for a lightweight physics
// world (no broadphase/joints here, that is PhysicsWorld2D).
class RigidBody2D {
public:
    enum class BodyType { Static, Kinematic, Dynamic };

    RigidBody2D() {}
    explicit RigidBody2D(float x, float y, float mass = 1.0f)
        : x_(x), y_(y), mass_(mass) {}

    void SetPosition(float x, float y) { x_ = x; y_ = y; }
    void Position(float& x, float& y) const { x = x_; y = y_; }

    void SetVelocity(float vx, float vy) { vx_ = vx; vy_ = vy; }
    void Velocity(float& vx, float& vy) const { vx = vx_; vy = vy_; }
    void SetGravityScale(float g) { gravityScale_ = g; }
    float GravityScale() const { return gravityScale_; }

    void SetMass(float m) { mass_ = m <= 0 ? 1 : m; }
    float Mass() const { return mass_; }
    float InvMass() const {
        if (type_ == BodyType::Static) return 0.0f;
        return 1.0f / mass_;
    }

    void SetType(BodyType t) { type_ = t; }
    BodyType Type() const { return type_; }

    void SetDamping(float linear, float angular = 0.0f) {
        linearDamping_ = linear; angularDamping_ = angular;
    }

    void ApplyForce(float fx, float fy) { fx_ += fx; fy_ += fy; }
    void ApplyImpulse(float jx, float jy) {
        if (type_ == BodyType::Static) return;
        vx_ += jx * InvMass();
        vy_ += jy * InvMass();
    }

    // Step dynamics by dt with gravity (gx, gy) and damping.
    void Step(float dt, float gx = 0.0f, float gy = -9.81f) {
        if (type_ != BodyType::Dynamic) return;
        vx_ += (fx_ * InvMass() + gx * gravityScale_) * dt;
        vy_ += (fy_ * InvMass() + gy * gravityScale_) * dt;
        vx_ *= std::max(0.0f, 1.0f - linearDamping_ * dt);
        vy_ *= std::max(0.0f, 1.0f - linearDamping_ * dt);
        x_ += vx_ * dt;
        y_ += vy_ * dt;
        // Reset accumulated forces after integration.
        fx_ = fy_ = 0.0f;
    }

private:
    float x_ = 0, y_ = 0;
    float vx_ = 0, vy_ = 0;
    float fx_ = 0, fy_ = 0;
    float mass_ = 1.0f;
    float gravityScale_ = 1.0f;
    float linearDamping_ = 0.0f, angularDamping_ = 0.0f;
    BodyType type_ = BodyType::Dynamic;
};

} // namespace bighero
