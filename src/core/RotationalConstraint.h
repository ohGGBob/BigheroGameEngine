#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// RotationalConstraint: a hinge/angular constraint limiting the relative angle
// between two bodies to a permitted range. Pure CPU-side, self-contained.
class RotationalConstraint {
public:
    RotationalConstraint() {}
    RotationalConstraint(std::size_t bodyA, std::size_t bodyB)
        : bodyA_(bodyA), bodyB_(bodyB) {}

    void SetBodies(std::size_t a, std::size_t b) { bodyA_=a; bodyB_=b; }
    std::size_t BodyA() const { return bodyA_; }
    std::size_t BodyB() const { return bodyB_; }

    void SetLimits(float min, float max) {
        if (min > max) { float t=min; min=max; max=t; }
        minAngle_=min; maxAngle_=max;
    }
    void LimitMin(float m) { minAngle_=m; }
    void LimitMax(float m) { maxAngle_=m; }
    float LimitMin() const { return minAngle_; }
    float LimitMax() const { return maxAngle_; }

    bool Locked() const { return locked_; }
    void SetLocked(bool l) { locked_=l; }

    // Is the given relative angle within the allowed range?
    bool WithinLimits(float relAngle) const {
        if (locked_) return std::fabs(relAngle) < 1e-6f;
        return relAngle >= minAngle_ && relAngle <= maxAngle_;
    }

    // How much does the angle violate the range (positive = violation)?
    float Violation(float relAngle) const {
        if (relAngle < minAngle_) return minAngle_ - relAngle;
        if (relAngle > maxAngle_) return relAngle - maxAngle_;
        return 0.0f;
    }

private:
    std::size_t bodyA_=0, bodyB_=0;
    float minAngle_=0.0f, maxAngle_=0.0f;
    bool locked_=false;
};

} // namespace bighero
