#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// Circle3: a 3D circle defined by center, radius and a plane normal.
// Self-contained, std-lib only.
class Circle3 {
public:
    Circle3() = default;
    Circle3(float cx, float cy, float cz, float radius)
        : cx_(cx), cy_(cy), cz_(cz), radius_(radius) {}

    float CenterX() const { return cx_; }
    float CenterY() const { return cy_; }
    float CenterZ() const { return cz_; }
    void SetCenter(float x, float y, float z) { cx_=x; cy_=y; cz_=z; }

    float Radius() const { return radius_; }
    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }

    // Plane normal the circle lies on (default +Z).
    void SetNormal(float nx, float ny, float nz) {
        float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (len > 0) { nx_=nx/len; ny_=ny/len; nz_=nz/len; }
        else { nx_=0; ny_=0; nz_=1; }
    }
    float NormalX() const { return nx_; }
    float NormalY() const { return ny_; }
    float NormalZ() const { return nz_; }

    float Circumference() const { return 6.2831853f * radius_; }
    float Area() const { return 3.14159265f * radius_ * radius_; }
    float Diameter() const { return radius_ * 2.0f; }

    // Point on the circle at angle theta (radians) around the plane.
    void PointAt(float theta, float& x, float& y, float& z) const {
        // Compute two orthonormal tangents via the normal.
        float tx = 1, ty = 0, tz = 0;
        if (std::fabs(nx_) > 0.9f) { tx = 0; ty = 1; tz = 0; }
        // v = normal x t
        float vx = ny_*tz - nz_*ty;
        float vy = nz_*tx - nx_*tz;
        float vz = nx_*ty - ny_*tx;
        float vl = std::sqrt(vx*vx + vy*vy + vz*vz);
        if (vl > 0) { vx/=vl; vy/=vl; vz/=vl; }
        // u = t (normalized)
        float tl = std::sqrt(tx*tx + ty*ty + tz*tz);
        tx/=tl; ty/=tl; tz/=tl;
        float c = std::cos(theta), s = std::sin(theta);
        x = cx_ + radius_*(c*tx + s*vx);
        y = cy_ + radius_*(c*ty + s*vy);
        z = cz_ + radius_*(c*tz + s*vz);
    }

    // Whether a point lies on the circle's plane (within tolerance).
    bool OnPlane(float px, float py, float pz, float tol = 1e-4f) const {
        float d = (px-cx_)*nx_ + (py-cy_)*ny_ + (pz-cz_)*nz_;
        return std::fabs(d) <= tol;
    }

private:
    float cx_ = 0, cy_ = 0, cz_ = 0;
    float radius_ = 1.0f;
    float nx_ = 0, ny_ = 0, nz_ = 1.0f;
};

} // namespace bighero
