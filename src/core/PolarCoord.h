#pragma once
#include <cmath>

namespace bighero {

// PolarCoord: a 2D polar coordinate (radius + angle) with conversion to and
// from Cartesian coordinates. Self-contained, std-lib only.
struct PolarCoord {
    float radius = 0;
    float angle = 0; // radians

    PolarCoord() = default;
    PolarCoord(float r, float a) : radius(r), angle(a) {}

    // Convert to Cartesian.
    float ToCartesianX() const { return radius * std::cos(angle); }
    float ToCartesianY() const { return radius * std::sin(angle); }

    static PolarCoord FromCartesian(float x, float y) {
        return PolarCoord(std::sqrt(x * x + y * y), std::atan2(y, x));
    }
    // Rotate the angle by a delta (radians), keeping radius.
    void Rotate(float delta) { angle += delta; }
    PolarCoord Rotated(float delta) const { return PolarCoord(radius, angle + delta); }
};

} // namespace bighero
