#pragma once
#include <cstdint>
#include <cmath>
#include <vector>
#include <array>

namespace bighero {

// Polyline3: a sequence of 3D vertices (dynamically allocated) forming an open
// polyline. Provides arc length, point-at-distance and closest-point queries.
// Self-contained, std-lib only.
class Polyline3 {
public:
    Polyline3() = default;
    void Clear() { verts_.clear(); }
    size_t Count() const { return verts_.size(); }
    bool IsEmpty() const { return verts_.empty(); }

    void Add(float x, float y, float z) { verts_.push_back({x,y,z}); }
    void Reserve(size_t n) { verts_.reserve(n); }
    const float* Vertex(size_t i) const { return verts_[i].data(); }

    // Total arc length.
    float Length() const {
        if (verts_.size() < 2) return 0;
        float total = 0;
        for (size_t i = 0; i+1 < verts_.size(); ++i) total += SegLength(i);
        return total;
    }

    bool PointAtDistance(float dist, float* out) const {
        if (verts_.empty()) return false;
        if (verts_.size() == 1) { out[0]=verts_[0][0]; out[1]=verts_[0][1]; out[2]=verts_[0][2]; return true; }
        if (dist <= 0) { out[0]=verts_[0][0]; out[1]=verts_[0][1]; out[2]=verts_[0][2]; return true; }
        float walked = 0;
        for (size_t i = 0; i+1 < verts_.size(); ++i) {
            float len = SegLength(i);
            if (dist <= walked + len) {
                float u = len > 0 ? (dist-walked)/len : 0;
                for (int c=0;c<3;++c) out[c] = verts_[i][c] + (verts_[i+1][c]-verts_[i][c])*u;
                return true;
            }
            walked += len;
        }
        out[0]=verts_[verts_.size()-1][0]; out[1]=verts_[verts_.size()-1][1]; out[2]=verts_[verts_.size()-1][2];
        return true;
    }

    // Closest point distance from a query point.
    float Distance(float px, float py, float pz) const {
        if (verts_.empty()) return huge();
        if (verts_.size() == 1) { float dx=px-verts_[0][0],dy=py-verts_[0][1],dz=pz-verts_[0][2]; return std::sqrt(dx*dx+dy*dy+dz*dz); }
        float best = huge();
        for (size_t i = 0; i+1 < verts_.size(); ++i) {
            float dx,dy,dz;
            SegPointDistance(i, px, py, pz, dx, dy, dz);
            if (dx<best) best=dx;
        }
        return best;
    }

private:
    std::vector<std::array<float,3>> verts_;
    static float huge() { return 1e30f; }
    float SegLength(size_t i) const {
        float dx=verts_[i+1][0]-verts_[i][0], dy=verts_[i+1][1]-verts_[i][1], dz=verts_[i+1][2]-verts_[i][2];
        return std::sqrt(dx*dx+dy*dy+dz*dz);
    }
    float SegPointDistance(size_t i, float px, float py, float pz, float& d,
                           float& cx, float& cy) const {
        const float* a = verts_[i].data();
        const float* b = verts_[i+1].data();
        float dx=b[0]-a[0], dy=b[1]-a[1], dz=b[2]-a[2];
        float len2=dx*dx+dy*dy+dz*dz;
        float t;
        if (len2<=0) t=0;
        else t=((px-a[0])*dx+(py-a[1])*dy+(pz-a[2])*dz)/len2;
        if (t<0)t=0;
        if (t>1)t=1;
        cx=a[0]+dx*t; cy=a[1]+dy*t; float cz=a[2]+dz*t;
        d=std::sqrt((px-cx)*(px-cx)+(py-cy)*(py-cy)+(pz-cz)*(pz-cz));
        return d;
    }
};

} // namespace bighero
