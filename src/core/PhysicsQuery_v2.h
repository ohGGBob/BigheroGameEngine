#pragma once
#include <cstdint>
#include <vector>
#include "CollisionShape_v2.h"

namespace bighero {

// PhysicsQuery: a container for physics overlap/raycast results and a small
// spatial-hash-free query helper for a list of shapes. Self-contained,
// std-lib only.
class PhysicsQuery {
public:
    PhysicsQuery() = default;

    void Clear() { results_.clear(); }
    size_t ResultCount() const { return results_.size(); }
    std::vector<CollisionShape>& Results() { return results_; }
    const std::vector<CollisionShape>& Results() const { return results_; }
    void AddResult(const CollisionShape& s) { results_.push_back(s); }

    // Raycast a set of axis-aligned boxes (min/max) and collect hits.
    // Each box is {minX,minY,minZ,maxX,maxY,maxZ,id}. Points the ray from origin
    // along dir into the first box whose t is in [0,maxT].
    bool RaycastBoxes(const float* origin, const float* dir, float maxT,
                      const std::vector<float>& boxes, int& hitId, float& hitT,
                      float& hitX, float& hitY, float& hitZ) const {
        bool hit = false;
        hitId = -1; hitT = maxT;
        for (size_t i = 0; i + 7 <= boxes.size(); i += 7) {
            float minX=boxes[i], minY=boxes[i+1], minZ=boxes[i+2];
            float maxX=boxes[i+3], maxY=boxes[i+4], maxZ=boxes[i+5];
            float id = boxes[i+6];
            // Slab test.
            float t0 = 0, t1 = maxT;
            bool inside = true;
            float inv[3] = { dir[0] != 0 ? 1.0f/dir[0] : 1e30f,
                             dir[1] != 0 ? 1.0f/dir[1] : 1e30f,
                             dir[2] != 0 ? 1.0f/dir[2] : 1e30f };
            for (int a = 0; a < 3; ++a) {
                float lo = a==0?minX:(a==1?minY:minZ);
                float hi = a==0?maxX:(a==1?maxY:maxZ);
                float o = a==0?origin[0]:(a==1?origin[1]:origin[2]);
                float tlo = (lo - o) * inv[a];
                float thi = (hi - o) * inv[a];
                if (tlo > thi) { float tmp=tlo; tlo=thi; thi=tmp; }
                if (tlo > t0) t0 = tlo;
                if (thi < t1) t1 = thi;
                if (t0 > t1) inside = false;
            }
            if (inside && t0 < hitT && t0 < maxT) {
                hit = true; hitId = (int)id; hitT = t0;
                hitX = origin[0] + dir[0]*t0;
                hitY = origin[1] + dir[1]*t0;
                hitZ = origin[2] + dir[2]*t0;
            }
        }
        return hit;
    }

    // Point-in-box test for a box {minX,minY,minZ,maxX,maxY,maxZ}.
    bool PointInBox(float px, float py, float pz, const float* box) const {
        return px >= box[0] && px <= box[3] &&
               py >= box[1] && py <= box[4] &&
               pz >= box[2] && pz <= box[5];
    }

private:
    std::vector<CollisionShape> results_;
};

} // namespace bighero
