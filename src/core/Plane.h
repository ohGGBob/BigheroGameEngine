#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// Plane: an infinite plane in Hessian normal form (n . p = d) for physics
// collision and clipping. Pure CPU-side, self-contained.
class Plane {
public:
    Plane() {}
    Plane(float nx, float ny, float nz, float d) {
        nx_ = nx; ny_ = ny; nz_ = nz; d_ = d;
        Normalize();
    }

    void SetNormal(float nx, float ny, float nz) {
        float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (len < 1e-9f) { nx_=0; ny_=0; nz_=1; return; }
        nx_=nx/len; ny_=ny/len; nz_=nz/len;
    }
    void Normal(float& nx, float& ny, float& nz) const { nx=nx_; ny=ny_; nz=nz_; }
    void SetDistance(float d) { d_ = d; }
    float Distance() const { return d_; }
    void SetPoint(float px, float py, float pz) { d_ = nx_*px + ny_*py + nz_*pz; }

    // Signed distance from a point (positive on the normal side).
    float SignedDistance(float px, float py, float pz) const {
        return nx_*px + ny_*py + nz_*pz - d_;
    }

    // Normalize the plane (make n unit and re-derive d).
    void Normalize() {
        float len = std::sqrt(nx_*nx_ + ny_*ny_ + nz_*nz_);
        if (len < 1e-9f) return;
        nx_/=len; ny_/=len; nz_/=len; d_/=len;
    }

    // Closest point on the plane to a given point.
    void ClosestPoint(float px, float py, float pz,
                      float& ox, float& oy, float& oz) const {
        float s = SignedDistance(px, py, pz);
        ox = px - nx_*s; oy = py - ny_*s; oz = pz - nz_*s;
    }

private:
    float nx_=0, ny_=0, nz_=1;
    float d_=0;
};

} // namespace bighero
