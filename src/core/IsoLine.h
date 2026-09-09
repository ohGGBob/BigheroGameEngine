#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// IsoLine: traces iso-level contour lines on a 2D scalar grid using the
// marching-squares algorithm. Outputs polyline segments as interleaved
// (x0,y0,x1,y1) floats. Standard-library only, self-contained.
class IsoLine {
public:
    IsoLine() {}
    IsoLine(float isoLevel) : iso_(isoLevel) {}

    void SetIso(float level) { iso_ = level; }
    float Iso() const { return iso_; }

    // field: (ny*nx) samples, x fastest. spacing: uniform cell size.
    void Generate(const std::vector<float>& field, std::size_t nx, std::size_t ny,
                  float spacing, std::vector<float>& segments) const {
        segments.clear();
        if (nx < 2 || ny < 2) return;
        if (field.size() < nx * ny) return;
        const float s = spacing;
        for (std::size_t y = 0; y+1 < ny; ++y)
            for (std::size_t x = 0; x+1 < nx; ++x) {
                float vbl = field[y*nx+x];
                float vbr = field[y*nx+(x+1)];
                float vtr = field[(y+1)*nx+(x+1)];
                float vtl = field[(y+1)*nx+x];
                int caseIndex = 0;
                if (vbl > iso_) caseIndex |= 1;
                if (vbr > iso_) caseIndex |= 2;
                if (vtr > iso_) caseIndex |= 4;
                if (vtl > iso_) caseIndex |= 8;
                if (caseIndex == 0 || caseIndex == 15) continue;
                float blx=x*s,     bly=y*s;
                float brx=(x+1)*s, bry=y*s;
                float trx=(x+1)*s, try_=(y+1)*s;
                float tlx=x*s,     tly=(y+1)*s;
                // Interpolate crossing points on the 4 edges.
                float bx,by, rx,ry, tx,ty, lx,ly;
                LerpCross(blx,bly,brx,bry, vbl,vbr, bx,by);   // bottom
                LerpCross(brx,bry,trx,try_, vbr,vtr, rx,ry);  // right
                LerpCross(tlx,tly,trx,try_, vtl,vtr, tx,ty);  // top
                LerpCross(blx,bly,tlx,tly, vbl,vtl, lx,ly);   // left
                switch (caseIndex) {
                    case 1:  case 14: Emit(segments, lx,ly, bx,by); break;
                    case 2:  case 13: Emit(segments, bx,by, rx,ry); break;
                    case 3:  case 12: Emit(segments, lx,ly, rx,ry); break;
                    case 4:  case 11: Emit(segments, rx,ry, tx,ty); break;
                    case 6:  case 9:  Emit(segments, bx,by, tx,ty); break;
                    case 7:  case 8:  Emit(segments, lx,ly, tx,ty); break;
                    case 5:  Emit(segments, lx,ly, bx,by); Emit(segments, rx,ry, tx,ty); break;
                    case 10: Emit(segments, bx,by, rx,ry); Emit(segments, lx,ly, tx,ty); break;
                    default: break;
                }
            }
    }

private:
    // Linearly interpolate the iso crossing point on edge (ax,ay)-(bx,by).
    void LerpCross(float ax,float ay, float bx,float by,
                   float va,float vb, float& ox,float& oy) const {
        float denom = vb - va;
        float t = std::fabs(denom) < 1e-9f ? 0.5f : (iso_ - va) / denom;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        ox = ax + (bx - ax) * t;
        oy = ay + (by - ay) * t;
    }
    static void Emit(std::vector<float>& seg, float x0,float y0,float x1,float y1) {
        seg.push_back(x0); seg.push_back(y0); seg.push_back(x1); seg.push_back(y1);
    }
    float iso_ = 0.5f;
};

} // namespace bighero
