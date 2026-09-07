#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// Ray intersection helper results and tests. Pure standard library.
struct RayHit {
    bool hit;
    float t;          // distance along ray
    float point[3];   // world hit point
    float normal[3];  // surface normal

    RayHit() : hit(false), t(0.0f), point{0,0,0}, normal{0,0,0} {}
};

class RaycastHelper {
public:
    // Ray vs plane (point + normal). Returns hit and t.
    static bool RayPlane(float ox, float oy, float oz,
                         float dx, float dy, float dz,
                         float px, float py, float pz,
                         float nx, float ny, float nz,
                         float& t) {
        float denom = nx * dx + ny * dy + nz * dz;
        if (std::fabs(denom) < 1e-10f) return false;
        t = (nx * (px - ox) + ny * (py - oy) + nz * (pz - oz)) / denom;
        return t > 0.0f;
    }

    // Ray vs axis-aligned box (slab method). Writes near/far.
    static bool RayAabb(float ox, float oy, float oz,
                        float dx, float dy, float dz,
                        float minx, float miny, float minz,
                        float maxx, float maxy, float maxz,
                        float& tEnter, float& tExit) {
        float t0 = 0.0f, t1 = 1e30f;
        float inv;
        if (std::fabs(dx) < 1e-10f) { if (ox < minx || ox > maxx) return false; }
        else { inv = 1.0f / dx;
               float ta = (minx - ox) * inv, tb = (maxx - ox) * inv;
               if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
               if (ta > t0) t0 = ta; if (tb < t1) t1 = tb; if (t0 > t1) return false; }
        if (std::fabs(dy) < 1e-10f) { if (oy < miny || oy > maxy) return false; }
        else { inv = 1.0f / dy;
               float ta = (miny - oy) * inv, tb = (maxy - oy) * inv;
               if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
               if (ta > t0) t0 = ta; if (tb < t1) t1 = tb; if (t0 > t1) return false; }
        if (std::fabs(dz) < 1e-10f) { if (oz < minz || oz > maxz) return false; }
        else { inv = 1.0f / dz;
               float ta = (minz - oz) * inv, tb = (maxz - oz) * inv;
               if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
               if (ta > t0) t0 = ta; if (tb < t1) t1 = tb; if (t0 > t1) return false; }
        tEnter = t0; tExit = t1;
        return true;
    }

    // Ray vs sphere. Writes near t.
    static bool RaySphere(float ox, float oy, float oz,
                          float dx, float dy, float dz,
                          float cx, float cy, float cz, float r,
                          float& t) {
        float lx = cx - ox, ly = cy - oy, lz = cz - oz;
        float tca = lx * dx + ly * dy + lz * dz;
        float d2 = (lx * lx + ly * ly + lz * lz) - tca * tca;
        float r2 = r * r;
        if (d2 > r2) return false;
        float thc = std::sqrt(r2 - d2);
        t = tca - thc;
        return t > 0.0f;
    }
};

} // namespace bighero
