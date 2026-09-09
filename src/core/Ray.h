#pragma once
#include <cstddef>
#include <cmath>

namespace bighero {

// Ray: a 3D ray (origin + direction) used for picking, physics raycasts, and
// visibility tests. Pure CPU-side, self-contained.
class Ray {
public:
    Ray() {}
    Ray(float ox, float oy, float oz, float dx, float dy, float dz)
        : ox_(ox), oy_(oy), oz_(oz), dx_(dx), dy_(dy), dz_(dz) {
        Normalize();
    }

    void SetOrigin(float ox, float oy, float oz) { ox_=ox; oy_=oy; oz_=oz; }
    void Origin(float& ox, float& oy, float& oz) const { ox=ox_; oy=oy_; oz=oz_; }
    void SetDirection(float dx, float dy, float dz) { dx_=dx; dy_=dy; dz_=dz; Normalize(); }
    void Direction(float& dx, float& dy, float& dz) const { dx=dx_; dy=dy_; dz=dz_; }

    void Normalize() {
        float len = std::sqrt(dx_*dx_ + dy_*dy_ + dz_*dz_);
        if (len < 1e-9f) { dx_=1; dy_=0; dz_=0; return; }
        dx_/=len; dy_/=len; dz_/=len;
    }

    // Point at parameter t along the ray.
    void At(float t, float& px, float& py, float& pz) const {
        px = ox_ + dx_*t; py = oy_ + dy_*t; pz = oz_ + dz_*t;
    }

    // Intersect with an axis-aligned box; returns t or -1 on miss.
    float IntersectAABB(float minX, float minY, float minZ,
                        float maxX, float maxY, float maxZ) const {
        float tmin = 0, tmax = 1e30f;
        float o[3] = {ox_,oy_,oz_}, d[3] = {dx_,dy_,dz_};
        float mn[3] = {minX,minY,minZ}, mx[3] = {maxX,maxY,maxZ};
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(d[i]) < 1e-9f) {
                if (o[i] < mn[i] || o[i] > mx[i]) return -1.0f;
            } else {
                float t1 = (mn[i]-o[i])/d[i];
                float t2 = (mx[i]-o[i])/d[i];
                if (t1 > t2) { float t=t1; t1=t2; t2=t; }
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) return -1.0f;
            }
        }
        return tmin;
    }

    // Intersect with a plane; returns t or -1 if parallel/behind.
    float IntersectPlane(float pnx, float pny, float pnz, float pd) const {
        float denom = pnx*dx_ + pny*dy_ + pnz*dz_;
        if (std::fabs(denom) < 1e-9f) return -1.0f;
        float t = (pd - (pnx*ox_ + pny*oy_ + pnz*oz_)) / denom;
        return t >= 0 ? t : -1.0f;
    }

private:
    float ox_=0, oy_=0, oz_=0;
    float dx_=1, dy_=0, dz_=0;
};

} // namespace bighero
