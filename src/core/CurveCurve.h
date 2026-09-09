#pragma once
#include <cmath>

namespace bighero {

// CurveCurve: intersection / distance / projection utilities between simple
// analytic 2D curves (lines, circles, arcs, and Bezier approximations).
// Standard-library only, self-contained.
class CurveCurve {
public:
    CurveCurve() = default;

    // Intersection of two line segments (p0->p1 and q0->q1). Returns true and
    // the intersection point if they intersect.
    static bool SegmentSegment(float p0x, float p0y, float p1x, float p1y,
                               float q0x, float q0y, float q1x, float q1y,
                               float& ix, float& iy) {
        float sx = p1x - p0x, sy = p1y - p0y;
        float tx = q1x - q0x, ty = q1y - q0y;
        float den = sx * ty - sy * tx;
        if (std::fabs(den) < 1e-9f) return false;
        float qpx = q0x - p0x, qpy = q0y - p0y;
        float u = (qpx * ty - qpy * tx) / den;
        float v = (qpx * sy - qpy * sx) / den;
        if (u < 0 || u > 1 || v < 0 || v > 1) return false;
        ix = p0x + u * sx; iy = p0y + u * sy;
        return true;
    }
    // Circle-line intersection: line p0->p1, circle center (cx,cy) radius r.
    static int LineCircle(float p0x, float p0y, float p1x, float p1y,
                          float cx, float cy, float r,
                          float& ox1, float& oy1, float& ox2, float& oy2) {
        float dx = p1x - p0x, dy = p1y - p0y;
        float fx = p0x - cx, fy = p0y - cy;
        float a = dx * dx + dy * dy;
        if (a < 1e-9f) return 0;
        float b = 2 * (fx * dx + fy * dy);
        float c = fx * fx + fy * fy - r * r;
        float disc = b * b - 4 * a * c;
        if (disc < 0) return 0;
        float sq = std::sqrt(disc);
        float t1 = (-b - sq) / (2 * a);
        float t2 = (-b + sq) / (2 * a);
        int count = 0;
        if (t1 >= 0 && t1 <= 1) { ox1 = p0x + t1 * dx; oy1 = p0y + t1 * dy; ++count; }
        if (t2 >= 0 && t2 <= 1) {
            if (count == 0) { ox1 = p0x + t2 * dx; oy1 = p0y + t2 * dy; }
            else { ox2 = p0x + t2 * dx; oy2 = p0y + t2 * dy; }
            ++count;
        }
        return count;
    }
    // Circle-circle intersection. Returns 0,1,2 solutions.
    static int CircleCircle(float c1x, float c1y, float r1,
                            float c2x, float c2y, float r2,
                            float& ox1, float& oy1, float& ox2, float& oy2) {
        float dx = c2x - c1x, dy = c2y - c1y;
        float d = std::sqrt(dx * dx + dy * dy);
        if (d < 1e-9f) return 0;
        if (d > r1 + r2 || d < std::fabs(r1 - r2)) return 0;
        float a = (r1 * r1 - r2 * r2 + d * d) / (2 * d);
        float h2 = r1 * r1 - a * a;
        if (h2 < 0) return 0;
        float h = std::sqrt(h2);
        float mx = c1x + a * dx / d, my = c1y + a * dy / d;
        float px = -dy / d * h, py = dx / d * h;
        ox1 = mx + px; oy1 = my + py;
        ox2 = mx - px; oy2 = my - py;
        return (h > 1e-9f) ? 2 : 1;
    }
};

} // namespace bighero
