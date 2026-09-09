#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// JointTransform: local position + rotation quaternion (w,x,y,z) + scale for a
// single joint. Self-contained / std-lib only.
class JointTransform {
public:
    JointTransform() = default;
    JointTransform(float px, float py, float pz, float qw=1, float qx=0, float qy=0, float qz=0)
        : px_(px), py_(py), pz_(pz), qw_(qw), qx_(qx), qy_(qy), qz_(qz) {}

    void SetPosition(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void SetRotation(float w, float x, float y, float z) { qw_=w; qx_=x; qy_=y; qz_=z; }
    void SetScale(float x, float y, float z) { sx_=x; sy_=y; sz_=z; }

    float X() const { return px_; } float Y() const { return py_; } float Z() const { return pz_; }
    float QW() const { return qw_; } float QX() const { return qx_; }
    float QY() const { return qy_; } float QZ() const { return qz_; }
    float SX() const { return sx_; } float SY() const { return sy_; } float SZ() const { return sz_; }

    void SetIdentity() { px_=py_=pz_=0; qw_=1; qx_=qy_=qz_=0; sx_=sy_=sz_=1; }
    bool IsIdentity() const { return px_==0&&py_==0&&pz_==0&&qw_==1&&qx_==0&&qy_==0&&qz_==0&&sx_==1&&sy_==1&&sz_==1; }

private:
    float px_=0, py_=0, pz_=0;
    float qw_=1, qx_=0, qy_=0, qz_=0;
    float sx_=1, sy_=1, sz_=1;
};

} // namespace bighero
