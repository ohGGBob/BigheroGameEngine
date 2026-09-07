#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// 2D signed distance field primitives. Each returns signed distance to a shape
// (negative inside, positive outside). Pure standard library.
class Sdf2D {
public:
    static float Circle(float px, float py, float cx, float cy, float r) {
        float dx = px - cx, dy = py - cy;
        return std::sqrt(dx * dx + dy * dy) - r;
    }

    static float Box(float px, float py, float cx, float cy, float hw, float hh) {
        float dx = std::fabs(px - cx) - hw;
        float dy = std::fabs(py - cy) - hh;
        float ox = std::max(dx, 0.0f), oy = std::max(dy, 0.0f);
        return std::sqrt(ox * ox + oy * oy) + std::min(std::max(dx, dy), 0.0f);
    }

    static float Segment(float px, float py, float ax, float ay, float bx, float by) {
        float abx = bx - ax, aby = by - ay;
        float apx = px - ax, apy = py - ay;
        float lenSq = abx * abx + aby * aby;
        float t = 0.0f;
        if (lenSq > 1e-10f) {
            t = (apx * abx + apy * aby) / lenSq;
            t = std::max(0.0f, std::min(1.0f, t));
        }
        float cx = ax + abx * t, cy = ay + aby * t;
        float dx = px - cx, dy = py - cy;
        return std::sqrt(dx * dx + dy * dy);
    }

    static float RoundedBox(float px, float py, float cx, float cy, float hw, float hh, float r) {
        float dx = std::fabs(px - cx) - (hw - r);
        float dy = std::fabs(py - cy) - (hh - r);
        float ox = std::max(dx, 0.0f), oy = std::max(dy, 0.0f);
        return std::sqrt(ox * ox + oy * oy) + std::min(std::max(dx, dy), 0.0f) - r;
    }

    static float Union(float a, float b) { return std::min(a, b); }
    static float Intersect(float a, float b) { return std::max(a, b); }
    static float Subtract(float a, float b) { return std::max(a, -b); } // a - b
};

} // namespace bighero
