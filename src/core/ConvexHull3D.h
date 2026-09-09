#pragma once
#include <vector>
#include <cstddef>
#include <cmath>
#include <algorithm>

namespace bighero {

// ConvexHull3D: computes an approximate convex hull of a point cloud in 3D.
// This simplified implementation projects onto a plane and returns the
// convex polygon's boundary points for a "2D slab" — used for debug/rough
// hulls. Pure CPU-side, self-contained.
class ConvexHull3D {
public:
    struct Point { float x, y, z; };

    ConvexHull3D() {}

    void Clear() { input_.clear(); hull_.clear(); }
    void AddPoint(float x, float y, float z) { input_.push_back({x, y, z}); }
    std::size_t InputCount() const { return input_.size(); }

    // Compute hull; projects to the XY plane and returns the boundary set
    // (a monotone-chain 2D convex hull). Output in hull_.
    std::size_t Compute() {
        hull_.clear();
        if (input_.size() < 3) { hull_ = input_; return hull_.size(); }
        std::vector<std::pair<float,float>> pts;
        pts.reserve(input_.size());
        for (auto& p : input_) pts.push_back({p.x, p.y});
        std::sort(pts.begin(), pts.end());
        // monotone chain
        std::vector<std::pair<float,float>> h(2*pts.size());
        int k = 0;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            while (k >= 2 && Cross(h[k-2], h[k-1], pts[i]) <= 0) --k;
            h[k++] = pts[i];
        }
        std::size_t lower = k;
        for (std::size_t i = pts.size()-1; i+1 > 0; --i) {
            while (k > (int)lower && Cross(h[k-2], h[k-1], pts[i]) <= 0) --k;
            h[k++] = pts[i];
        }
        h.resize(k > 0 ? k-1 : 0);
        hull_.reserve(h.size());
        for (auto& p : h) hull_.push_back({p.first, p.second, 0.0f});
        return hull_.size();
    }

    std::size_t HullCount() const { return hull_.size(); }
    bool GetHullPoint(std::size_t i, Point& out) const {
        if (i >= hull_.size()) return false;
        out = hull_[i]; return true;
    }

private:
    static float Cross(const std::pair<float,float>& o,
                       const std::pair<float,float>& a,
                       const std::pair<float,float>& b) {
        return (a.first-o.first)*(b.second-o.second) -
               (a.second-o.second)*(b.first-o.first);
    }
    std::vector<Point> input_;
    std::vector<Point> hull_;
};

} // namespace bighero
