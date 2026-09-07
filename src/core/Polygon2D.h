#pragma once
#include <vector>
#include <cmath>

namespace bighero {

// Simple 2D polygon defined by a list of vertices (CCW or CW).
struct Polygon2D {
    std::vector<float> xs, ys;

    void Clear() { xs.clear(); ys.clear(); }

    void AddPoint(float x, float y) { xs.push_back(x); ys.push_back(y); }

    std::size_t Count() const { return xs.size(); }

    bool Empty() const { return xs.empty(); }

    // Ray-casting point-in-polygon test.
    bool Contains(float px, float py) const {
        std::size_t n = xs.size();
        if (n < 3) return false;
        bool inside = false;
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            float xi = xs[i], yi = ys[i];
            float xj = xs[j], yj = ys[j];
            if (((yi > py) != (yj > py)) &&
                (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) {
                inside = !inside;
            }
        }
        return inside;
    }

    // Approximate area via the shoelace formula.
    float Area() const {
        std::size_t n = xs.size();
        if (n < 3) return 0.0f;
        float sum = 0.0f;
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            sum += (xs[j] + xs[i]) * (ys[j] - ys[i]);
        }
        return std::fabs(sum) * 0.5f;
    }
};

} // namespace bighero
