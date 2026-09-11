#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// QuaternionTransform: minimal quaternion (w,x,y,z) with helpers for slerp-style
// interpolation used by animation. Self-contained / std-lib only.
class QuaternionTransform {
public:
    QuaternionTransform() = default;
    QuaternionTransform(float w, float x, float y, float z) : w_(w), x_(x), y_(y), z_(z) {}

    void Set(float w, float x, float y, float z) { w_=w; x_=x; y_=y; z_=z; }
    void SetIdentity() { w_=1; x_=y_=z_=0; }
    float W() const { return w_; } float X() const { return x_; }
    float Y() const { return y_; } float Z() const { return z_; }

    bool IsIdentity() const { return w_==1 && x_==0 && y_==0 && z_==0; }
    float Length() const { return w_*w_+x_*x_+y_*y_+z_*z_; }
    void Normalize() {
        float l = std::sqrt(Length());
        if (l > 0.0f) { float inv = 1.0f / l; w_*=inv; x_*=inv; y_*=inv; z_*=inv; }
        else SetIdentity();
    }
    // Linear interpolation toward target by t, then normalize.
    void Lerp(const QuaternionTransform& to, float t) {
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        w_ += (to.w_ - w_) * t; x_ += (to.x_ - x_) * t;
        y_ += (to.y_ - y_) * t; z_ += (to.z_ - z_) * t;
        Normalize();
    }
    static QuaternionTransform Identity() { return QuaternionTransform(1,0,0,0); }

private:
    float w_=1, x_=0, y_=0, z_=0;
};

} // namespace bighero
