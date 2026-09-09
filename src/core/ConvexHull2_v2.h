#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>

namespace bighero {

// ConvexHull2: computes the convex hull of a set of 2D points via Andrew's
// monotone chain algorithm. Result is counter-clockwise. Self-contained,
// std-lib only.
class ConvexHull2 {
public:
    struct Point { float x, y; };

    // Rebuild the hull from a set of points (with optional duplicates removed).
    // Returns the hull vertices in CCW order (no repeated closing point).
    static void Compute(const std::vector<Point>& pts, std::vector<Point>& hull) {
        hull.clear();
        size_t n = pts.size();
        if (n < 3) { hull = pts; return; }

        std::vector<Point> sorted = pts;
        std::sort(sorted.begin(), sorted.end(), [](const Point& a, const Point& b){
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });
        // Remove duplicates.
        std::vector<Point> uniq;
        for (auto& p : sorted) {
            if (uniq.empty() || uniq.back().x != p.x || uniq.back().y != p.y) uniq.push_back(p);
        }
        if (uniq.size() < 3) { hull = uniq; return; }

        std::vector<Point> lower;
        for (auto& p : uniq) {
            while (lower.size() >= 2 && Cross(lower[lower.size()-2], lower.back(), p) <= 0)
                lower.pop_back();
            lower.push_back(p);
        }
        std::vector<Point> upper;
        for (size_t i = uniq.size(); i-- > 0; ) {
            Point p = uniq[i];
            while (upper.size() >= 2 && Cross(upper[upper.size()-2], upper.back(), p) <= 0)
                upper.pop_back();
            upper.push_back(p);
        }
        lower.pop_back();
        upper.pop_back();
        hull.insert(hull.end(), lower.begin(), lower.end());
        hull.insert(hull.end(), upper.begin(), upper.end());
    }

    // Is a point inside (or on) the convex hull? Hull must be CCW.
    static bool Contains(const std::vector<Point>& hull, Point p) {
        if (hull.size() < 3) return false;
        for (size_t i = 0; i < hull.size(); ++i) {
            Point a = hull[i];
            Point b = hull[(i+1)%hull.size()];
            if (Cross(a, b, p) < 0) return false;
        }
        return true;
    }

    // Signed area of the hull (>0 CCW, <0 CW).
    static float Area(const std::vector<Point>& hull) {
        if (hull.size() < 3) return 0;
        float s = 0;
        for (size_t i = 0; i < hull.size(); ++i) {
            Point a = hull[i];
            Point b = hull[(i+1)%hull.size()];
            s += a.x*b.y - a.y*b.x;
        }
        return s * 0.5f;
    }

private:
    static float Cross(const Point& o, const Point& a, const Point& b) {
        return (a.x-o.x)*(b.y-o.y) - (a.y-o.y)*(b.x-o.x);
    }
};

} // namespace bighero
