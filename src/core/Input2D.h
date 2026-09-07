#pragma once
#include <cmath>

namespace bighero {

// 2D analog/stick input state.
struct Input2D {
    float x = 0, y = 0;

    void Set(float px, float py) { x = px; y = py; }
    void Reset() { x = y = 0; }

    float Magnitude() const { return std::sqrt(x*x + y*y); }
    float AngleRad() const { return std::atan2(y, x); }

    // Clamp magnitude to [0,maxMagnitude].
    void Clamp(float maxMagnitude) {
        float m = Magnitude();
        if (m > maxMagnitude && m > 0) {
            x = x / m * maxMagnitude;
            y = y / m * maxMagnitude;
        }
    }

    // Normalize to unit length (or zero if too short).
    void Normalize() {
        float m = Magnitude();
        if (m > 0) { x /= m; y /= m; }
    }

    // Dot product with another stick.
    float Dot(const Input2D& o) const { return x*o.x + y*o.y; }
};

} // namespace bighero
