#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// PolygonClipper: Sutherland-Hodgman polygon clipping against an axis-aligned
// half-space / box. Standard-library only, self-contained. Operates on 2D
// (x,y) vertex lists.
class PolygonClipper {
public:
    PolygonClipper() {}

    // Clip a convex polygon against a single half-space: keep points where
    // n·p >= d.
    static std::vector<float> ClipHalfSpace(
        const std::vector<float>& verts,  // interleaved x,y
        float nx, float ny, float d) {
        std::vector<float> out;
        std::size_t n = verts.size() / 2;
        if (n == 0) return out;
        auto inside = [&](float x, float y) { return nx*x + ny*y - d >= -1e-6f; };
        auto intersect = [&](float x1, float y1, float x2, float y2) {
            float denom = nx*(x2-x1) + ny*(y2-y1);
            if (std::fabs(denom) < 1e-9f) return std::pair<float,float>(x1,y1);
            float t = (d - (nx*x1 + ny*y1)) / denom;
            return std::pair<float,float>(x1 + t*(x2-x1), y1 + t*(y2-y1));
        };
        for (std::size_t i=0;i<n;++i) {
            std::size_t j = (i+1)%n;
            float x1=verts[2*i], y1=verts[2*i+1];
            float x2=verts[2*j], y2=verts[2*j+1];
            bool in1 = inside(x1,y1), in2 = inside(x2,y2);
            if (in1) { out.push_back(x1); out.push_back(y1); }
            if (in1 != in2) {
                auto p = intersect(x1,y1,x2,y2);
                out.push_back(p.first); out.push_back(p.second);
            }
        }
        return out;
    }

    // Clip against an axis-aligned box [minX,minY,maxX,maxY].
    static std::vector<float> ClipAABB(const std::vector<float>& verts,
                                       float minX, float minY, float maxX, float maxY) {
        std::vector<float> cur = verts;
        if (cur.size() < 6) return cur;
        cur = ClipHalfSpace(cur,  1, 0, minX);
        cur = ClipHalfSpace(cur, -1, 0, -maxX);
        cur = ClipHalfSpace(cur,  0, 1, minY);
        cur = ClipHalfSpace(cur,  0,-1, -maxY);
        return cur;
    }

    static float PolygonArea(const std::vector<float>& verts) {
        std::size_t n = verts.size()/2;
        if (n < 3) return 0.0f;
        float area = 0.0f;
        for (std::size_t i=0;i<n;++i) {
            std::size_t j=(i+1)%n;
            area += verts[2*i]*verts[2*j+1] - verts[2*j]*verts[2*i+1];
        }
        return 0.5f * std::fabs(area);
    }
};

} // namespace bighero
