#pragma once
#include <cmath>

namespace bighero {

// CharacterController: a capsule-shaped move controller that provides
// movement integration, gravity, grounded state and collision height
// bookkeeping. CPU-side pure functions; no physics backend.
class CharacterController {
public:
    CharacterController() {}
    CharacterController(float radius, float height)
        : radius_(radius < 0 ? 0 : radius), height_(height < 0 ? 0 : height) {}

    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }
    void SetHeight(float h) { height_ = h < 0 ? 0 : h; }
    float Height() const { return height_; }

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Position(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }

    void SetVelocity(float vx, float vy, float vz) { vx_=vx; vy_=vy; vz_=vz; }
    void Velocity(float& vx, float& vy, float& vz) const { vx=vx_; vy=vy_; vz=vz_; }

    // Move (horizontal only) by input direction; returns resulting displacement.
    void Move(float inputX, float inputY, float speed) {
        float len = std::sqrt(inputX*inputX + inputY*inputY);
        if (len > 0) { px_ += (inputX/len)*speed; pz_ += (inputY/len)*speed; }
        moved_ = true;
    }

    // Apply gravity for dt; sets grounded when y hits ground level.
    void ApplyGravity(float dt, float groundY = 0.0f, float gravity = -9.8f) {
        vy_ += gravity * dt;
        py_ += vy_ * dt;
        if (py_ <= groundY) { py_ = groundY; vy_ = 0; grounded_ = true; }
        else grounded_ = false;
    }

    void Jump(float impulse) { if (grounded_) { vy_ = impulse; grounded_ = false; } }
    bool IsGrounded() const { return grounded_; }
    void SetGrounded(bool g) { grounded_ = g; }

    void MovePosition(float dx, float dy, float dz) { px_+=dx; py_+=dy; pz_+=dz; }
    bool HasMoved() const { return moved_; }
    void ResetMoved() { moved_ = false; }

private:
    float radius_ = 0.5f, height_ = 2.0f;
    float px_=0, py_=0, pz_=0;
    float vx_=0, vy_=0, vz_=0;
    bool grounded_ = false;
    bool moved_ = false;
};

} // namespace bighero
