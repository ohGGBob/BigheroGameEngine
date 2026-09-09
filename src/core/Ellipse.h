#pragma once
#include <cmath>

namespace bighero {

// Ellipse: a 2D axis-aligned ellipse with point/parametric evaluation,
// containment, and SVG-style arc approximation helpers.
struct Ellipse {
    float cx = 0, cy = 0;
    float rx = 1, ry = 1;

    Ellipse() = default;
    Ellipse(float cx_, float cy_, float rx_, float ry_)
        : cx(cx_), cy(cy_), rx(rx_), ry(ry_) {}

    // Normalized (unit) ellipse point at parametric angle t.
    void Point(float t, float& ox, float& oy) const {
        ox = cx + rx * std::cos(t);
        oy = cy + ry * std::sin(t);
    }
    bool Contains(float px, float py) const {
        if (rx <= 0 || ry <= 0) return false;
        float dx = (px - cx) / rx;
        float dy = (py - cy) / ry;
        return (dx * dx + dy * dy) <= 1.0f;
    }
    float Area() const { return 3.14159265f * rx * ry; }
    float Perimeter() const {
        // Ramanujan approximation.
        float a = rx, b = ry;
        float h = (a - b) / (a + b);
        return 3.14159265f * (a + b) * (1 + 3 * h / (10 + std::sqrt(4 - 3 * h * h)));
    }
    void Bounds(float& xmin, float& ymin, float& xmax, float& ymax) const {
        xmin = cx - rx; ymin = cy - ry; xmax = cx + rx; ymax = cy + ry;
    }
};

} // namespace bighero
