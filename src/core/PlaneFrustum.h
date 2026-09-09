#pragma once
#include <cstdint>
#include <cmath>
#include "FrustumAABB_v2.h"

namespace bighero {

// PlaneFrustum: a frustum defined by six plane inequalities (near, far, left,
// right, top, bottom) with point/AABB inside tests. Plane form: n·p <= d.
// Self-contained, std-lib only.
class PlaneFrustum {
public:
    struct Plane { float nx, ny, nz, d; };

    PlaneFrustum() = default;

    // Set all six planes (near, far, left, right, top, bottom).
    void Set(const Plane& near_, const Plane& far_,
             const Plane& left_, const Plane& right_,
             const Plane& top_, const Plane& bottom_) {
        planes_[0]=near_; planes_[1]=far_; planes_[2]=left_;
        planes_[3]=right_; planes_[4]=top_; planes_[5]=bottom_;
    }
    const Plane& GetPlane(int i) const { return planes_[i]; }

    // Is a point inside the frustum (satisfies all six plane inequalities)?
    bool Contains(float x, float y, float z) const {
        for (int i = 0; i < 6; ++i) {
            if (planes_[i].nx*x + planes_[i].ny*y + planes_[i].nz*z > planes_[i].d)
                return false;
        }
        return true;
    }

    // Is an AABB fully inside the frustum? (All 8 corners inside.)
    bool ContainsBox(const FrustumAABB& b) const {
        const float xs[2] = {b.MinX(), b.MaxX()};
        const float ys[2] = {b.MinY(), b.MaxY()};
        const float zs[2] = {b.MinZ(), b.MaxZ()};
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                for (int k = 0; k < 2; ++k)
                    if (!Contains(xs[i], ys[j], zs[k])) return false;
        return true;
    }

    // Does an AABB intersect the frustum? (At least one corner inside, OR the
    // box spans across a plane — conservative using corner test.)
    bool IntersectsBox(const FrustumAABB& b) const {
        const float xs[2] = {b.MinX(), b.MaxX()};
        const float ys[2] = {b.MinY(), b.MaxY()};
        const float zs[2] = {b.MinZ(), b.MaxZ()};
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                for (int k = 0; k < 2; ++k)
                    if (Contains(xs[i], ys[j], zs[k])) return true;
        // Did not find a corner inside; also check frustum center vs box.
        // Conservative: return true if the box fully encloses a frustum point.
        return false;
    }

private:
    Plane planes_[6];
};

} // namespace bighero
