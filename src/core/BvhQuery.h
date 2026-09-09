#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// BvhQuery: performs ray / point / AABB queries against a pre-built bounding
// volume hierarchy (array of nodes with child indices). CPU-side traversal.
// Self-contained; nodes are provided externally as a flat vector.
class BvhQuery {
public:
    struct Node {
        float minX,minY,minZ,maxX,maxY,maxZ;
        int left, right;
        bool leaf;
        std::size_t start, count;
    };

    BvhQuery() {}

    void SetNodes(const std::vector<Node>& nodes, int root) {
        nodes_ = &nodes; root_ = root;
    }
    bool HasTree() const { return nodes_ && root_ >= 0; }

    struct Hit {
        int nodeIndex;
        float distance;
        bool hit;
    };

    // Ray vs AABB slab test.
    static bool RayBox(const float* o, const float* d,
                       float minX,float minY,float minZ,
                       float maxX,float maxY,float maxZ,
                       float maxDist, float& tOut) {
        float tmin = 0, tmax = maxDist;
        float oArr[3] = {o[0], o[1], o[2]};
        float dArr[3] = {d[0], d[1], d[2]};
        float mn[3] = {minX,minY,minZ}, mx[3] = {maxX,maxY,maxZ};
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(dArr[i]) < 1e-9f) {
                if (oArr[i] < mn[i] || oArr[i] > mx[i]) return false;
            } else {
                float t1 = (mn[i] - oArr[i]) / dArr[i];
                float t2 = (mx[i] - oArr[i]) / dArr[i];
                if (t1 > t2) std::swap(t1, t2);
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) return false;
            }
        }
        tOut = tmin;
        return true;
    }

    // Nearest leaf hit for a ray (returns the first box intersected).
    Hit QueryRay(const float* o, const float* d, float maxDist) const {
        Hit best{-1, maxDist, false};
        if (!HasTree()) return best;
        Traverse(root_, o, d, maxDist, best);
        return best;
    }

    bool IntersectsPoint(float x, float y, float z) const {
        if (!HasTree()) return false;
        return PointTraverse(root_, x, y, z);
    }

private:
    void Traverse(int idx, const float* o, const float* d, float maxDist, Hit& best) const {
        const Node& n = (*nodes_)[idx];
        float t;
        if (!RayBox(o, d, n.minX,n.minY,n.minZ,n.maxX,n.maxY,n.maxZ, best.distance, t))
            return;
        if (n.leaf) {
            if (t < best.distance) { best.distance = t; best.nodeIndex = idx; best.hit = true; }
            return;
        }
        if (n.left >= 0) Traverse(n.left, o, d, maxDist, best);
        if (n.right >= 0) Traverse(n.right, o, d, maxDist, best);
    }
    bool PointTraverse(int idx, float x, float y, float z) const {
        const Node& n = (*nodes_)[idx];
        if (x < n.minX || x > n.maxX || y < n.minY || y > n.maxY || z < n.minZ || z > n.maxZ)
            return false;
        if (n.leaf) return true;
        return (n.left >= 0 && PointTraverse(n.left, x, y, z)) ||
               (n.right >= 0 && PointTraverse(n.right, x, y, z));
    }

    const std::vector<Node>* nodes_ = nullptr;
    int root_ = -1;
};

} // namespace bighero
