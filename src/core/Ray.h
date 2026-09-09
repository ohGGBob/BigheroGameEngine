#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// 3D ray: origin + direction (direction expected normalized for distance semantics).
struct Ray {
    float ox, oy, oz;
    float dx, dy, dz;

    Ray() : ox(0), oy(0), oz(0), dx(0), dy(0), dz(1) {}

    Ray(float ox_, float oy_, float oz_, float dx_, float dy_, float dz_)
        : ox(ox_), oy(oy_), oz(oz_), dx(dx_), dy(dy_), dz(dz_) {}

    void GetPoint(float t, float& px, float& py, float& pz) const {
        px = ox + dx * t; py = oy + dy * t; pz = oz + dz * t;
    }

    void NormalizeDirection() {
        float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1e-8f) { dx = 0; dy = 0; dz = 1; return; }
        dx /= len; dy /= len; dz /= len;
    }

    // Ray vs sphere intersection. Returns true and writes near/far t.
    bool IntersectSphere(const float cx, const float cy, const float cz, float radius,
                         float& tNear, float& tFar) const {
        float lx = cx - ox, ly = cy - oy, lz = cz - oz;
        float tca = lx * dx + ly * dy + lz * dz;
        float d2 = (lx * lx + ly * ly + lz * lz) - tca * tca;
        float r2 = radius * radius;
        if (d2 > r2) return false;
        float thc = std::sqrt(r2 - d2);
        tNear = tca - thc;
        tFar = tca + thc;
        return true;
    }

    // Ray vs axis-aligned box (min/max). Slab method.
    bool IntersectAabb(const float minx, const float miny, const float minz,
                       const float maxx, const float maxy, const float maxz,
                       float& tEnter, float& tExit) const {
        float t0 = 0.0f, t1 = 1e30f;
        float inv;
        if (std::abs(dx) < 1e-8f) {
            if (ox < minx || ox > maxx) return false;
        } else {
            inv = 1.0f / dx; float ta = (minx - ox) * inv, tb = (maxx - ox) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
        if (std::abs(dy) < 1e-8f) {
            if (oy < miny || oy > maxy) return false;
        } else {
            inv = 1.0f / dy; float ta = (miny - oy) * inv, tb = (maxy - oy) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
        if (std::abs(dz) < 1e-8f) {
            if (oz < minz || oz > maxz) return false;
        } else {
            inv = 1.0f / dz; float ta = (minz - oz) * inv, tb = (maxz - oz) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) return false;
        }
        tEnter = t0; tExit = t1;
        return true;
    }
};

} // namespace bighero
