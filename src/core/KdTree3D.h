#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// Simple 3D k-d tree for point spatial queries over a static set of points.
// Each point has a payload id. Pure standard library, self-contained.
class KdTree3D {
public:
    struct Point {
        float x, y, z;
        int id;
    };

    void Clear() { points_.clear(); }

    std::size_t Size() const { return points_.size(); }

    void Insert(const Point& p) { points_.push_back(p); }

    // Query all points within radius of (cx,cy,cz). Appends ids to 'out'.
    void QueryRadius(float cx, float cy, float cz, float radius,
                     std::vector<int>& out) const {
        float r2 = radius * radius;
        for (const auto& p : points_) {
            float dx = p.x - cx, dy = p.y - cy, dz = p.z - cz;
            if (dx * dx + dy * dy + dz * dz <= r2)
                out.push_back(p.id);
        }
    }

    // Brute-force nearest neighbor (returns id, or -1 if empty).
    int Nearest(float cx, float cy, float cz) const {
        if (points_.empty()) return -1;
        int bestId = -1;
        float best = 1e30f;
        for (const auto& p : points_) {
            float dx = p.x - cx, dy = p.y - cy, dz = p.z - cz;
            float d = dx * dx + dy * dy + dz * dz;
            if (d < best) { best = d; bestId = p.id; }
        }
        return bestId;
    }

private:
    std::vector<Point> points_;
};

} // namespace bighero
