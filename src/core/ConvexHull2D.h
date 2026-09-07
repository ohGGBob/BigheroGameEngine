#pragma once
#include <vector>
#include <algorithm>
#include <cmath>

namespace bighero {

// 2D convex hull (Andrew's monotone chain). Returns hull vertices in CCW order.
class ConvexHull2D {
public:
    struct Pt { float x, y; };

    static std::vector<Pt> Compute(std::vector<Pt> points) {
        std::vector<Pt> hull;
        if (points.size() < 3) return hull;
        std::sort(points.begin(), points.end(), [](const Pt& a, const Pt& b){
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
        // remove duplicates
        points.erase(std::unique(points.begin(), points.end(),
            [](const Pt& a, const Pt& b){ return a.x==b.x && a.y==b.y; }), points.end());
        if (points.size() < 3) return points;
        std::vector<Pt> lower, upper;
        for (auto& p : points) {
            while (lower.size() >= 2 && Cross(lower[lower.size()-2], lower.back(), p) <= 0)
                lower.pop_back();
            lower.push_back(p);
        }
        for (int i = (int)points.size()-1; i >= 0; --i) {
            const Pt& p = points[i];
            while (upper.size() >= 2 && Cross(upper[upper.size()-2], upper.back(), p) <= 0)
                upper.pop_back();
            upper.push_back(p);
        }
        lower.pop_back(); upper.pop_back();
        hull.insert(hull.end(), lower.begin(), lower.end());
        hull.insert(hull.end(), upper.begin(), upper.end());
        return hull;
    }

    // Signed area (positive if CCW).
    static float SignedArea(const std::vector<Pt>& poly) {
        float area = 0;
        for (size_t i = 0; i < poly.size(); ++i) {
            const Pt& a = poly[i];
            const Pt& b = poly[(i+1)%poly.size()];
            area += a.x*b.y - b.x*a.y;
        }
        return area * 0.5f;
    }

private:
    static float Cross(const Pt& o, const Pt& a, const Pt& b) {
        return (a.x-o.x)*(b.y-o.y) - (a.y-o.y)*(b.x-o.x);
    }
};

} // namespace bighero
