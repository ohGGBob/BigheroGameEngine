#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// PolygonTriangulator: an ear-clipping triangulator for a simple (non-
// self-intersecting) 2D polygon. Outputs flat (a,b,c) triangle indices into
// the input counter-clockwise vertex list. Standard-library only,
// self-contained.
class PolygonTriangulator {
public:
    PolygonTriangulator() {}

    // verts: interleaved (x,y). Fills triangles as flat index triples.
    void Triangulate(const std::vector<float>& verts, std::vector<unsigned>& triangles) {
        triangles.clear();
        std::size_t n = verts.size() / 2;
        if (n < 3) return;
        if (!IsCCW(verts)) {
            // Work on a reversed index list to ensure CCW.
            std::vector<unsigned> idx(n);
            for (std::size_t i=0;i<n;++i) idx[i]=(unsigned)(n-1-i);
            EarClipIndexed(verts, idx, triangles);
            return;
        }
        std::vector<unsigned> idx(n);
        for (std::size_t i=0;i<n;++i) idx[i]=(unsigned)i;
        EarClipIndexed(verts, idx, triangles);
    }

    static bool IsCCW(const std::vector<float>& verts) {
        std::size_t n = verts.size()/2;
        if (n < 3) return true;
        float area = 0;
        for (std::size_t i=0;i<n;++i) {
            std::size_t j=(i+1)%n;
            area += verts[2*i]*verts[2*j+1] - verts[2*j]*verts[2*i+1];
        }
        return area > 0;
    }

private:
    static float Cross(const std::vector<float>& v, unsigned a, unsigned b, unsigned c) {
        float ax=v[2*a], ay=v[2*a+1], bx=v[2*b], by=v[2*b+1], cx=v[2*c], cy=v[2*c+1];
        return (bx-ax)*(cy-ay) - (by-ay)*(cx-ax);
    }
    static bool Inside(const std::vector<float>& v, unsigned a, unsigned b, unsigned c, unsigned p) {
        bool s1 = Cross(v,a,b,p) >= 0;
        bool s2 = Cross(v,b,c,p) >= 0;
        bool s3 = Cross(v,c,a,p) >= 0;
        return s1 && s2 && s3;
    }
    static bool IsEar(const std::vector<float>& v, unsigned a, unsigned b, unsigned c,
                      const std::vector<unsigned>& idx) {
        if (Cross(v,a,b,c) <= 1e-9f) return false;  // non-convex vertex
        for (std::size_t i=0;i<idx.size();++i) {
            unsigned p = idx[i];
            if (p==a||p==b||p==c) continue;
            if (Inside(v,a,b,c,p)) return false;
        }
        return true;
    }
    static void EarClipIndexed(const std::vector<float>& v, std::vector<unsigned>& idx,
                               std::vector<unsigned>& triangles) {
        while (idx.size() > 3) {
            bool clipped = false;
            for (std::size_t i=0;i<idx.size();++i) {
                unsigned a=idx[(i+idx.size()-1)%idx.size()];
                unsigned b=idx[i];
                unsigned c=idx[(i+1)%idx.size()];
                if (IsEar(v,a,b,c,idx)) {
                    triangles.push_back(a);
                    triangles.push_back(b);
                    triangles.push_back(c);
                    idx.erase(idx.begin()+i);
                    clipped = true;
                    break;
                }
            }
            if (!clipped) break;  // degenerate; stop to avoid infinite loop
        }
        if (idx.size()==3) {
            triangles.push_back(idx[0]);
            triangles.push_back(idx[1]);
            triangles.push_back(idx[2]);
        }
    }
};

} // namespace bighero
