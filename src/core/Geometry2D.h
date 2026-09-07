#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace bighero {

// 2D geometry polygon/triangle helper functions (pure math, no dependency).
class Geometry2D {
public:
    struct Pt { float x, y; };

    // Signed area of a polygon (positive if CCW).
    static float SignedArea(const std::vector<Pt>& poly) {
        float area = 0;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const Pt& a = poly[i];
            const Pt& b = poly[(i + 1) % poly.size()];
            area += a.x * b.y - b.x * a.y;
        }
        return area * 0.5f;
    }

    // Triangle area.
    static float TriangleArea(const Pt& a, const Pt& b, const Pt& c) {
        return std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5f;
    }

    // Barycentric coordinates of point p in triangle (a,b,c).
    static void Barycentric(const Pt& a, const Pt& b, const Pt& c, const Pt& p,
                            float& u, float& v, float& w) {
        float det = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::fabs(det) < 1e-12f) { u = v = w = 0; return; }
        u = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / det;
        v = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / det;
        w = 1 - u - v;
    }

    // Is point p inside triangle (a,b,c) (including edges)?
    static bool PointInTriangle(const Pt& a, const Pt& b, const Pt& c, const Pt& p) {
        float u, v, w;
        Barycentric(a, b, c, p, u, v, w);
        const float eps = 1e-6f;
        return u >= -eps && v >= -eps && w >= -eps;
    }

    // Circumcenter of a triangle; returns false if degenerate.
    static bool Circumcenter(const Pt& a, const Pt& b, const Pt& c, Pt& out) {
        float d = 2.0f * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
        if (std::fabs(d) < 1e-12f) return false;
        float a2 = a.x*a.x + a.y*a.y;
        float b2 = b.x*b.x + b.y*b.y;
        float c2 = c.x*c.x + c.y*c.y;
        out.x = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d;
        out.y = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d;
        return true;
    }

    // Is a point inside a convex polygon (CCW)? Uses signed cross tests.
    static bool PointInConvexPolygon(const std::vector<Pt>& poly, const Pt& p) {
        if (poly.size() < 3) return false;
        float sign = 0;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const Pt& a = poly[i];
            const Pt& b = poly[(i + 1) % poly.size()];
            float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
            if (std::fabs(cross) < 1e-9f) continue;
            if (sign == 0) sign = cross > 0 ? 1 : -1;
            else if ((cross > 0 ? 1 : -1) != sign) return false;
        }
        return true;
    }

    // Centroid of a polygon (works for convex/concave via area weighting).
    static Pt PolygonCentroid(const std::vector<Pt>& poly) {
        Pt c{0, 0};
        float areaSum = 0;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const Pt& a = poly[i];
            const Pt& b = poly[(i + 1) % poly.size()];
            float cross = a.x * b.y - b.x * a.y;
            c.x += (a.x + b.x) * cross;
            c.y += (a.y + b.y) * cross;
            areaSum += cross;
        }
        if (std::fabs(areaSum) < 1e-12f) {
            for (auto& p : poly) { c.x += p.x / poly.size(); c.y += p.y / poly.size(); }
            return c;
        }
        areaSum *= 0.5f;
        c.x /= (6.0f * areaSum);
        c.y /= (6.0f * areaSum);
        return c;
    }
};

} // namespace bighero
