#pragma once
#include <cmath>

namespace bighero {

// 3D triangle with three vertices.
struct Triangle3D {
    float ax, ay, az, bx, by, bz, cx, cy, cz;

    Triangle3D() : ax(0), ay(0), az(0), bx(1), by(0), bz(0), cx(0), cy(1), cz(0) {}
    Triangle3D(float ax_, float ay_, float az_, float bx_, float by_, float bz_,
               float cx_, float cy_, float cz_)
        : ax(ax_), ay(ay_), az(az_), bx(bx_), by(by_), bz(bz_),
          cx(cx_), cy(cy_), cz(cz_) {}

    // Cross product of edges AB x AC = area-normal.
    void Normal(float& nx, float& ny, float& nz) const {
        float ux = bx-ax, uy = by-ay, uz = bz-az;
        float vx = cx-ax, vy = cy-ay, vz = cz-az;
        nx = uy*vz - uz*vy;
        ny = uz*vx - ux*vz;
        nz = ux*vy - uy*vx;
        float len = std::sqrt(nx*nx + ny*ny + nz*nz);
        if (len > 1e-10f) { nx/=len; ny/=len; nz/=len; }
    }

    float Area() const {
        float ux = bx-ax, uy = by-ay, uz = bz-az;
        float vx = cx-ax, vy = cy-ay, vz = cz-az;
        float cx_ = uy*vz - uz*vy;
        float cy_ = uz*vx - ux*vz;
        float cz_ = ux*vy - uy*vx;
        return std::sqrt(cx_*cx_ + cy_*cy_ + cz_*cz_) * 0.5f;
    }

    // Ray (origin o, dir d) vs triangle, Moller-Trumbore. Writes t and barycentric (u,v).
    bool IntersectRay(float ox, float oy, float oz,
                      float dx, float dy, float dz,
                      float& t, float& u, float& v) const {
        float e1x = bx-ax, e1y = by-ay, e1z = bz-az;
        float e2x = cx-ax, e2y = cy-ay, e2z = cz-az;
        float px = dy*e2z - dz*e2y;
        float py = dz*e2x - dx*e2z;
        float pz = dx*e2y - dy*e2x;
        float det = e1x*px + e1y*py + e1z*pz;
        if (std::abs(det) < 1e-10f) return false;
        float invDet = 1.0f / det;
        float sx = ox-ax, sy = oy-ay, sz = oz-az;
        float bx_ = sx*px + sy*py + sz*pz;
        u = bx_ * invDet;
        if (u < 0.0f || u > 1.0f) return false;
        float qx = sy*e1z - sz*e1y;
        float qy = sz*e1x - sx*e1z;
        float qz = sx*e1y - sy*e1x;
        float by_ = dx*qx + dy*qy + dz*qz;
        v = by_ * invDet;
        if (v < 0.0f || u + v > 1.0f) return false;
        float bz_ = e2x*qx + e2y*qy + e2z*qz;
        t = bz_ * invDet;
        return t > 0.0f;
    }
};

} // namespace bighero
