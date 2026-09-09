#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// NavigationPath: an ordered sequence of 3D waypoints for navmesh/graph path
// following. Provides length, point access, and segment helpers. Pure stdlib.
class NavigationPath {
public:
    struct Point { float x, y, z; };

    NavigationPath() {}

    void Clear() { points_.clear(); }
    void AddPoint(float x, float y, float z) { points_.push_back({x,y,z}); }
    std::size_t Count() const { return points_.size(); }

    bool Get(std::size_t i, float& x, float& y, float& z) const {
        if (i >= points_.size()) return false;
        x = points_[i].x; y = points_[i].y; z = points_[i].z;
        return true;
    }
    const Point& At(std::size_t i) const { return points_[i]; }

    // Total path length (sum of segment distances).
    float Length() const {
        float len = 0;
        for (std::size_t i = 1; i < points_.size(); ++i)
            len += Dist(points_[i-1], points_[i]);
        return len;
    }

    bool IsEmpty() const { return points_.empty(); }
    bool HasMultiplePoints() const { return points_.size() > 1; }

    // Simple bounds check: returns area-normalized longest segment.
    float LongestSegment() const {
        float m = 0;
        for (std::size_t i = 1; i < points_.size(); ++i) {
            float d = Dist(points_[i-1], points_[i]);
            if (d > m) m = d;
        }
        return m;
    }

private:
    static float Dist(const Point& a, const Point& b) {
        float dx=b.x-a.x, dy=b.y-a.y, dz=b.z-a.z;
        return std::sqrt(dx*dx+dy*dy+dz*dz);
    }

    std::vector<Point> points_;
};

} // namespace bighero
