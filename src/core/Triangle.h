#pragma once
#include <cmath>

namespace bighero
{

// Triangle: a 2D triangle with area, centroid, barycentric containment,
// and circumcenter helpers. Standard-library only, self-contained.
struct Triangle
{
    float ax = 0, ay = 0;
    float bx = 0, by = 0;
    float cx = 0, cy = 0;

    Triangle() = default;
    Triangle(float ax_, float ay_, float bx_, float by_, float cx_, float cy_)
        : ax(ax_), ay(ay_), bx(bx_), by(by_), cx(cx_), cy(cy_)
    {
    }

    double Area() const { return std::fabs((double)(bx - ax) * (cy - ay) - (cx - ax) * (by - ay)) * 0.5; }
    void Centroid(float& ox, float& oy) const
    {
        ox = (ax + bx + cx) / 3.0f;
        oy = (ay + by + cy) / 3.0f;
    }
    // Barycentric containment test.
    bool Contains(float px, float py) const
    {
        float v0x = cx - ax, v0y = cy - ay;
        float v1x = bx - ax, v1y = by - ay;
        float v2x = px - ax, v2y = py - ay;
        float den = v0x * v1y - v1x * v0y;
        if (std::fabs((double)den) < 1e-12)
            return false;
        float u = (v2x * v1y - v1x * v2y) / den;
        float v = (v0x * v2y - v2x * v0y) / den;
        return u >= 0 && v >= 0 && (u + v) <= 1;
    }
    // Circumcenter (intersection of perpendicular bisectors).
    void Circumcenter(float& ox, float& oy) const
    {
        float d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
        if (std::fabs(d) < 1e-9f)
        {
            ox = ax;
            oy = ay;
            return;
        }
        float a2 = ax * ax + ay * ay;
        float b2 = bx * bx + by * by;
        float c2 = cx * cx + cy * cy;
        ox = (a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d;
        oy = (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d;
    }
};

} // namespace bighero
