#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// RaycastHit: the result of a raycast: hit point, normal, distance, and the id
// of the hit body/collider. Self-contained, std-lib only.
class RaycastHit {
public:
    RaycastHit() = default;

    void SetHit(bool h) { hit_ = h; }
    bool HasHit() const { return hit_; }
    operator bool() const { return hit_; }

    void SetPoint(float x, float y, float z) { px_=x; py_=y; pz_=z; }
    float PointX() const { return px_; }
    float PointY() const { return py_; }
    float PointZ() const { return pz_; }

    void SetNormal(float nx, float ny, float nz) { nx_=nx; ny_=ny; nz_=nz; }
    float NormalX() const { return nx_; }
    float NormalY() const { return ny_; }
    float NormalZ() const { return nz_; }

    void SetDistance(float d) { distance_ = d; }
    float Distance() const { return distance_; }

    void SetColliderId(int id) { colliderId_ = id; }
    int ColliderId() const { return colliderId_; }

    void Reset() {
        hit_ = false; px_=py_=pz_=nx_=ny_=nz_=distance_=0; colliderId_ = -1;
    }

    // Direction from origin to hit point given the ray origin (for convenience).
    void ComputeNormalIfZero(const float* origin) {
        if (nx_==0 && ny_==0 && nz_==0 && distance_>0) {
            float dx = px_-origin[0], dy = py_-origin[1], dz = pz_-origin[2];
            float len = std::sqrt(dx*dx+dy*dy+dz*dz);
            if (len>0) { nx_=dx/len; ny_=dy/len; nz_=dz/len; }
        }
    }

private:
    bool hit_ = false;
    float px_=0, py_=0, pz_=0;
    float nx_=0, ny_=0, nz_=0;
    float distance_ = 0;
    int colliderId_ = -1;
};

} // namespace bighero
