#pragma once
#include <vector>
#include <cmath>

namespace bighero {

// 2D base shape with containment & area (modular geometry helpers).
class Shape2D {
public:
    struct Pt { float x, y; };

    // Point-in-polygon test (works for convex & concave; even-odd rule).
    static bool PointInPolygon(const std::vector<Pt>& poly, float px, float py) {
        if (poly.size() < 3) return false;
        bool inside = false;
        size_t n = poly.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            const Pt& a = poly[i];
            const Pt& b = poly[j];
            if ((a.y > py) != (b.y > py)) {
                float xint = a.x + (py - a.y) * (b.x - a.x) / (b.y - a.y);
                if (px < xint) inside = !inside;
            }
        }
        return inside;
    }

    // Is point inside a circle.
    static bool PointInCircle(float cx, float cy, float r, float px, float py) {
        float dx = px - cx, dy = py - cy;
        return dx*dx + dy*dy <= r*r;
    }

    // Is point inside an axis-aligned rect.
    static bool PointInRect(float rx, float ry, float rw, float rh, float px, float py) {
        return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
    }

    // Polygon area (signed).
    static float PolygonArea(const std::vector<Pt>& poly) {
        float area = 0;
        for (size_t i = 0; i < poly.size(); ++i) {
            const Pt& a = poly[i];
            const Pt& b = poly[(i + 1) % poly.size()];
            area += a.x * b.y - b.x * a.y;
        }
        return area * 0.5f;
    }
};

} // namespace bighero
