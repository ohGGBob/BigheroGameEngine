#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// LookAtConstraint: a transform constraint that aims a bone/object's forward
// axis toward a target point, with an optional up axis and roll. Pure math +
// config; the backend applies the resulting direction.
class LookAtConstraint {
public:
    LookAtConstraint() {}
    LookAtConstraint(float tx, float ty, float tz)
        : tx_(tx), ty_(ty), tz_(tz) {}

    void SetTarget(float x, float y, float z) { tx_=x; ty_=y; tz_=z; }
    void Target(float& x, float& y, float& z) const { x=tx_; y=ty_; z=tz_; }
    void SetWeight(float w) { weight_ = w < 0 ? 0 : (w > 1 ? 1 : w); }
    float Weight() const { return weight_; }
    void SetUpAxis(float x, float y, float z) {
        float len = std::sqrt(x*x+y*y+z*z);
        if (len > 1e-8f) { upx_=x/len; upy_=y/len; upz_=z/len; }
        else { upx_=0; upy_=1; upz_=0; }
    }
    void SetRoll(float deg) { roll_ = deg; }
    float Roll() const { return roll_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    // Compute the forward direction from an origin toward the target.
    void Direction(float ox, float oy, float oz,
                   float& dx, float& dy, float& dz) const {
        dx = tx_-ox; dy = ty_-oy; dz = tz_-oz;
        float len = std::sqrt(dx*dx+dy*dy+dz*dz);
        if (len > 1e-8f) { dx/=len; dy/=len; dz/=len; }
        else { dx=0; dy=0; dz=1; }
    }

private:
    float tx_=0, ty_=0, tz_=1;
    float weight_ = 1.0f;
    float upx_=0, upy_=1, upz_=0;
    float roll_ = 0.0f;
    bool enabled_ = true;
};

} // namespace bighero
