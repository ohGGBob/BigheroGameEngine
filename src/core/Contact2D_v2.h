#pragma once
#include <cstdint>

namespace bighero {

// Contact2D: a single 2D contact point between two bodies, with position,
// normal, penetration depth and applied normal impulse.
// Self-contained, std-lib only.
class Contact2D {
public:
    Contact2D() = default;

    void SetPosition(float x, float y) { px_=x; py_=y; }
    float PositionX() const { return px_; }
    float PositionY() const { return py_; }

    void SetNormal(float nx, float ny) { nx_=nx; ny_=ny; }
    float NormalX() const { return nx_; }
    float NormalY() const { return ny_; }

    void SetPenetration(float p) { penetration_ = p; }
    float Penetration() const { return penetration_; }

    void SetNormalImpulse(float i) { normalImpulse_ = i; }
    float NormalImpulse() const { return normalImpulse_; }
    void SetTangentImpulse(float i) { tangentImpulse_ = i; }
    float TangentImpulse() const { return tangentImpulse_; }

    void SetBodyA(int id) { bodyA_ = id; }
    void SetBodyB(int id) { bodyB_ = id; }
    int BodyA() const { return bodyA_; }
    int BodyB() const { return bodyB_; }

    void Reset() {
        px_=py_=nx_=ny_=penetration_=normalImpulse_=tangentImpulse_=0;
        bodyA_=-1; bodyB_=-1;
    }
    bool IsValid() const { return penetration_ > 0 || normalImpulse_ != 0; }

    // Apply an impulse along the contact normal.
    void ApplyNormalImpulse(float impulse) { normalImpulse_ += impulse; }

private:
    float px_ = 0, py_ = 0;
    float nx_ = 0, ny_ = 0;
    float penetration_ = 0;
    float normalImpulse_ = 0, tangentImpulse_ = 0;
    int bodyA_ = -1, bodyB_ = -1;
};

} // namespace bighero
