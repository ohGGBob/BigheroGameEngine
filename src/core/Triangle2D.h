#pragma once
#include <algorithm>

namespace bighero {

// 2D triangle with three vertices (a, b, c).
struct Triangle2D {
    float ax, ay, bx, by, cx, cy;

    Triangle2D() : ax(0), ay(0), bx(1), by(0), cx(0), cy(1) {}
    Triangle2D(float ax_, float ay_, float bx_, float by_, float cx_, float cy_)
        : ax(ax_), ay(ay_), bx(bx_), by(by_), cx(cx_), cy(cy_) {}

    // Barycentric point-in-triangle test.
    bool Contains(float px, float py) const {
        float d1 = Sign(px, py, ax, ay, bx, by);
        float d2 = Sign(px, py, bx, by, cx, cy);
        float d3 = Sign(px, py, cx, cy, ax, ay);
        bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
        bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
        return !(hasNeg && hasPos);
    }

    float Area() const {
        return std::abs((bx-ax)*(cy-ay) - (cx-ax)*(by-ay)) * 0.5f;
    }

    // Compute barycentric weights (u,v,w) for point p.
    void Barycentric(float px, float py, float& u, float& v, float& w) const {
        float denom = (by-cy)*(ax-cx) + (cx-bx)*(ay-cy);
        if (std::abs(denom) < 1e-10f) { u = v = w = 0; return; }
        w = ((by-cy)*(px-cx) + (cx-bx)*(py-cy)) / denom;
        v = ((cy-ay)*(px-cx) + (ax-cx)*(py-cy)) / denom;
        u = 1.0f - v - w;
    }

private:
    static float Sign(float px, float py, float x1, float y1, float x2, float y2) {
        return (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2);
    }
};

} // namespace bighero
