#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// DelaunayTriangulation: a brute-force O(n^3) Delaunay triangulation of a 2D
// point set. For every triple of points, if its circumcircle contains no other
// point, that triple is an (empty-circle) Delaunay triangle. Outputs flat
// (a,b,c) index triples into the input point array. Standard-library only,
// self-contained. Intended for modest point counts (tens to low hundreds).
class DelaunayTriangulation {
public:
    DelaunayTriangulation() {}

    // Triangulate points (px,py). Fills triangles as flat (a,b,c) index triples.
    void Triangulate(const std::vector<float>& px, const std::vector<float>& py,
                     std::vector<unsigned>& triangles) {
        triangles.clear();
        std::size_t n = px.size() < py.size() ? px.size() : py.size();
        if (n < 3) return;
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = i+1; j < n; ++j)
                for (std::size_t k = j+1; k < n; ++k) {
                    // Circumcircle center/radius for (i,j,k).
                    float cx, cy, r2;
                    if (!Circumcircle(px[i],py[i], px[j],py[j], px[k],py[k], cx, cy, r2))
                        continue;   // collinear (degenerate) — skip
                    bool empty = true;
                    if (empty) {
                        for (std::size_t m = 0; m < n; ++m) {
                            if (m == i || m == j || m == k) continue;
                            float dx = px[m]-cx, dy = py[m]-cy;
                            if (dx*dx + dy*dy < r2 - 1e-9f) { empty = false; break; }
                        }
                    }
                    if (empty) {
                        triangles.push_back((unsigned)i);
                        triangles.push_back((unsigned)j);
                        triangles.push_back((unsigned)k);
                    }
                }
    }

private:
    // Returns false if the 3 points are (near-)collinear.
    static bool Circumcircle(float x1,float y1, float x2,float y2, float x3,float y3,
                             float& cx, float& cy, float& r2) {
        float ax = x1-x3, ay = y1-y3;
        float bx = x2-x3, by = y2-y3;
        float d = 2.0f * (ax*by - ay*bx);
        if (std::fabs(d) < 1e-9f) return false;
        float ux = (ax*ax + ay*ay) * by - (bx*bx + by*by) * ay;
        float uy = (bx*bx + by*by) * ax - (ax*ax + ay*ay) * bx;
        cx = x3 + ux / d;
        cy = y3 + uy / d;
        float dx = x1-cx, dy = y1-cy;
        r2 = dx*dx + dy*dy;
        return true;
    }
};

} // namespace bighero
