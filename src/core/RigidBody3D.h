#pragma once
#include <cstddef>

namespace bighero {

// RigidBody3D: dynamic/kinematic rigid body descriptor — mass, velocity,
// angular velocity, gravity scale, and sleep state. Pure CPU-side data +
// helpers; integration is provided by the physics backend.
class RigidBody3D {
public:
    enum class Type { Static, Kinematic, Dynamic };

    RigidBody3D() {}
    RigidBody3D(float mass, Type type = Type::Dynamic)
        : mass_(mass < 0 ? 0 : mass), type_(type) {}

    void SetMass(float m) { mass_ = m < 0 ? 0 : m; }
    float Mass() const { return mass_; }
    float InverseMass() const { return (mass_ > 1e-8f) ? 1.0f/mass_ : 0.0f; }
    void SetType(Type t) { type_ = t; }
    Type CurrentType() const { return type_; }
    bool IsDynamic() const { return type_ == Type::Dynamic; }

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }
    void SetVelocity(float x, float y, float z) { vx_=x; vy_=y; vz_=z; }
    void Velocity(float& x, float& y, float& z) const { x=vx_; y=vy_; z=vz_; }
    void SetAngularVelocity(float x, float y, float z) { wx_=x; wy_=y; wz_=z; }
    void AngularVelocity(float& x, float& y, float& z) const { x=wx_; y=wy_; z=wz_; }

    void SetGravityScale(float s) { gravityScale_ = s; }
    float GravityScale() const { return gravityScale_; }
    void SetLinearDamping(float d) { linearDamping_ = d < 0 ? 0 : d; }
    float LinearDamping() const { return linearDamping_; }
    void SetAngularDamping(float d) { angularDamping_ = d < 0 ? 0 : d; }
    float AngularDamping() const { return angularDamping_; }

    // Simple semi-implicit integration step (dt seconds).
    void Integrate(float dt) {
        if (!IsDynamic() || dt <= 0) return;
        vy_ -= 9.81f * gravityScale_ * dt;  // gravity along -Y
        float damp = 1.0f / (1.0f + linearDamping_ * dt);
        vx_ *= damp; vy_ *= damp; vz_ *= damp;
        px_ += vx_ * dt; py_ += vy_ * dt; pz_ += vz_ * dt;
        wx_ *= (1.0f / (1.0f + angularDamping_ * dt));
        wy_ *= (1.0f / (1.0f + angularDamping_ * dt));
        wz_ *= (1.0f / (1.0f + angularDamping_ * dt));
    }

    void SetSleeping(bool s) { sleeping_ = s; }
    bool IsSleeping() const { return sleeping_; }
    void WakeUp() { sleeping_ = false; }
    void Sleep() { sleeping_ = true; }

    void ApplyImpulse(float ix, float iy, float iz) {
        if (!IsDynamic()) return;
        float im = InverseMass();
        vx_ += ix * im; vy_ += iy * im; vz_ += iz * im;
    }

private:
    float mass_ = 1.0f;
    float px_=0, py_=0, pz_=0;
    float vx_=0, vy_=0, vz_=0;
    float wx_=0, wy_=0, wz_=0;
    Type type_ = Type::Dynamic;
    float gravityScale_ = 1.0f;
    float linearDamping_ = 0.0f, angularDamping_ = 0.0f;
    bool sleeping_ = false;
};

} // namespace bighero
