#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// Plane3: a 3D plane defined by a normal and a distance 'd' along the normal
// (the plane satisfies n·p = d). Self-contained, std-lib only.
class Plane3 {
public:
    Plane3() = default;
    Plane3(float nx, float ny, float nz, float d) : nx_(nx), ny_(ny), nz_(nz), d_(d) {
        Normalize();
    }

    void Set(float nx, float ny, float nz, float d) {
        nx_=nx; ny_=ny; nz_=nz; d_=d; Normalize();
    }
    float NormalX() const { return nx_; }
    float NormalY() const { return ny_; }
    float NormalZ() const { return nz_; }
    float DistanceFromOrigin() const { return d_; }

    // Signed distance from a point to the plane (positive in front).
    float SignedDistance(float px, float py, float pz) const {
        return nx_*px + ny_*py + nz_*pz - d_;
    }
    float AbsDistance(float px, float py, float pz) const {
        return std::fabs(SignedDistance(px, py, pz));
    }

    // Project a point onto the plane.
    void Project(float& px, float& py, float& pz) const {
        float s = SignedDistance(px, py, pz);
        px -= nx_*s; py -= ny_*s; pz -= nz_*s;
    }

    // Which side a point is on (>0 front, <0 back, ==0 on plane).
    int Side(float px, float py, float pz, float tol = 1e-6f) const {
        float s = SignedDistance(px, py, pz);
        if (s > tol) return 1;
        if (s < -tol) return -1;
        return 0;
    }

    // Flip the normal and offset.
    void Flip() { nx_=-nx_; ny_=-ny_; nz_=-nz_; d_=-d_; }

    // Intersect a ray (origin + t*direction) with the plane; returns t or -1.
    float RayIntersect(float ox, float oy, float oz, float dx, float dy, float dz, float& t) const {
        float denom = nx_*dx + ny_*dy + nz_*dz;
        if (std::fabs(denom) < 1e-9f) return -1; // parallel
        t = (d_ - (nx_*ox + ny_*oy + nz_*oz)) / denom;
        return t;
    }

    // Distance from origin to plane.
    float PlaneDistance() const { return std::fabs(d_); }

private:
    void Normalize() {
        float len = std::sqrt(nx_*nx_ + ny_*ny_ + nz_*nz_);
        if (len > 0) { nx_/=len; ny_/=len; nz_/=len; d_/=len; }
        else { nx_=0; ny_=0; nz_=1; d_=0; }
    }
    float nx_=0, ny_=0, nz_=1.0f, d_=0;
};

} // namespace bighero
