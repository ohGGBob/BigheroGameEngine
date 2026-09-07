#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// 2D line segment between two endpoints.
struct Segment2D {
    float ax, ay, bx, by;

    Segment2D() : ax(0), ay(0), bx(1), by(0) {}
    Segment2D(float ax_, float ay_, float bx_, float by_) : ax(ax_), ay(ay_), bx(bx_), by(by_) {}

    float Length() const { return std::sqrt((bx-ax)*(bx-ax) + (by-ay)*(by-ay)); }
    float LengthSq() const { return (bx-ax)*(bx-ax) + (by-ay)*(by-ay); }

    // Closest point on the segment to (px, py); write out.
    void ClosestPoint(float px, float py, float& ox, float& oy) const {
        float vx = bx - ax, vy = by - ay;
        float lenSq = vx * vx + vy * vy;
        float t = 0.0f;
        if (lenSq > 1e-10f) {
            t = ((px - ax) * vx + (py - ay) * vy) / lenSq;
            t = std::max(0.0f, std::min(1.0f, t));
        }
        ox = ax + vx * t; oy = ay + vy * t;
    }

    float DistanceToPoint(float px, float py) const {
        float cx, cy; ClosestPoint(px, py, cx, cy);
        return std::sqrt((px-cx)*(px-cx) + (py-cy)*(py-cy));
    }

    bool Contains(float px, float py) const {
        return DistanceToPoint(px, py) <= 1e-5f;
    }
};

} // namespace bighero
