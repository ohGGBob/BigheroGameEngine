#pragma once
#include <cstddef>

namespace bighero {

// ContactPoint: a single contact between two colliders — world point, normal,
// penetration depth, and the ids of the two involved bodies. Pure data record.
class ContactPoint {
public:
    ContactPoint() {}
    ContactPoint(float px, float py, float pz, float nx, float ny, float nz,
                 float depth, std::size_t a, std::size_t b)
        : px_(px),py_(py),pz_(pz),nx_(nx),ny_(ny),nz_(nz),depth_(depth),bodyA_(a),bodyB_(b) {}

    void SetPoint(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    void Point(float& x, float& y, float& z) const { x=px_; y=py_; z=pz_; }
    void SetNormal(float x, float y, float z) {
        float len = normalize(x, y, z);
        if (len > 1e-8f) { nx_=x/len; ny_=y/len; nz_=z/len; }
        else { nx_=0; ny_=1; nz_=0; }
    }
    void Normal(float& x, float& y, float& z) const { x=nx_; y=ny_; z=nz_; }
    void SetDepth(float d) { depth_ = d < 0 ? 0 : d; }
    float Depth() const { return depth_; }

    void SetBodies(std::size_t a, std::size_t b) { bodyA_=a; bodyB_=b; }
    std::size_t BodyA() const { return bodyA_; }
    std::size_t BodyB() const { return bodyB_; }

    void SetSeparation(float s) { separation_ = s; }
    float Separation() const { return separation_; }

private:
    static float normalize(float x, float y, float z) {
        return sqrtf(x*x + y*y + z*z);
    }
    float px_=0, py_=0, pz_=0;
    float nx_=0, ny_=1, nz_=0;
    float depth_=0, separation_=0;
    std::size_t bodyA_=0, bodyB_=0;
};

} // namespace bighero
