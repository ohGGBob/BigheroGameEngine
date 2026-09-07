#pragma once
#include <cmath>

namespace bighero {

// 2D gravity attractor / repulsor acting on a point mass.
struct Attractor {
    float x, y;
    float strength; // >0 attract, <0 repel
    float falloff;  // 1/r^falloff; 2 ~ gravity

    Attractor() : x(0), y(0), strength(1.0f), falloff(2.0f) {}
    Attractor(float x_, float y_, float s, float f = 2.0f)
        : x(x_), y(y_), strength(s), falloff(f) {}

    float ForceAt(float px, float py, float& fx, float& fy) {
        float dx = x - px, dy = y - py;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1e-5f) { fx = fy = 0.0f; return 0.0f; }
        float denom = std::pow(dist, falloff);
        float mag = strength / (denom + 1e-6f);
        fx = dx / dist * mag;
        fy = dy / dist * mag;
        return mag;
    }
};

} // namespace bighero
