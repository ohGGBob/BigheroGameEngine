#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// SolverConstraint: a single per-contact constraint to be resolved by the
// physics solver — body ids, accumulated impulse, normal, and bias. Pure data
// record used during iterative solving.
class SolverConstraint {
public:
    SolverConstraint() {}
    SolverConstraint(std::size_t bodyA, std::size_t bodyB)
        : bodyA_(bodyA), bodyB_(bodyB) {}

    void SetBodies(std::size_t a, std::size_t b) { bodyA_=a; bodyB_=b; }
    std::size_t BodyA() const { return bodyA_; }
    std::size_t BodyB() const { return bodyB_; }

    void SetNormal(float nx, float ny, float nz) { nx_=nx; ny_=ny; nz_=nz; }
    void Normal(float& nx, float& ny, float& nz) const { nx=nx_; ny=ny_; nz=nz_; }

    void SetBias(float b) { bias_ = b; }
    float Bias() const { return bias_; }
    void SetMassNormal(float m) { massNormal_ = m; }
    float MassNormal() const { return massNormal_; }

    void AddImpulse(float j) { accumulated_ += j; }
    float AccumulatedImpulse() const { return accumulated_; }
    void ZeroImpulse() { accumulated_ = 0.0f; }

    void SetMaxImpulse(float m) { maxImpulse_ = m; }
    float MaxImpulse() const { return maxImpulse_; }

    // Clamp impulse to the non-negative range with restitution floor.
    float ClampImpulse(float j) const {
        if (j < 0.0f) j = 0.0f;
        if (j > maxImpulse_) j = maxImpulse_;
        return j;
    }

private:
    std::size_t bodyA_=0, bodyB_=0;
    float nx_=0, ny_=0, nz_=1;
    float bias_=0, massNormal_=1.0f;
    float accumulated_=0, maxImpulse_=1e9f;
};

} // namespace bighero
