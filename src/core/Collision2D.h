#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// 2D collision primitives (discrete). Pure standard library, self-contained.
class Collision2D {
public:
    // Circle vs circle overlap.
    static bool CircleCircle(float ax, float ay, float ar,
                             float bx, float by, float br) {
        float dx = bx - ax, dy = by - ay;
        float rs = ar + br;
        return (dx * dx + dy * dy) <= rs * rs;
    }

    // Point vs circle.
    static bool PointCircle(float px, float py, float cx, float cy, float r) {
        float dx = px - cx, dy = py - cy;
        return (dx * dx + dy * dy) <= r * r;
    }

    // AABB (min/max) vs AABB overlap.
    static bool AabbAabb(float ax, float ay, float aw, float ah,
                         float bx, float by, float bw, float bh) {
        return !(bx >= ax + aw || bx + bw <= ax || by >= ay + ah || by + bh <= ay);
    }

    // Point vs AABB (top-left + w/h).
    static bool PointAabb(float px, float py, float x, float y, float w, float h) {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    // Segment (a-b) vs circle. True if any point of the segment is within radius.
    static bool SegmentCircle(float ax, float ay, float bx, float by,
                              float cx, float cy, float r) {
        float abx = bx - ax, aby = by - ay;
        float acx = cx - ax, acy = cy - ay;
        float lenSq = abx * abx + aby * aby;
        float t = 0.0f;
        if (lenSq > 1e-10f) {
            t = (acx * abx + acy * aby) / lenSq;
            t = std::max(0.0f, std::min(1.0f, t));
        }
        float px = ax + abx * t, py = ay + aby * t;
        float dx = cx - px, dy = cy - py;
        return (dx * dx + dy * dy) <= r * r;
    }
};

} // namespace bighero
