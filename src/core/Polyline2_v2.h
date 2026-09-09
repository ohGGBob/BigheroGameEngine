#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// Polyline2: a sequence of 2D vertices forming an open polyline. Provides
// arc length, midpoint, point-at-distance and closest-point queries.
// Self-contained, std-lib only.
class Polyline2 {
public:
    Polyline2() = default;

    void Add(float x, float y) { if (count_ < (size_t)capacity) { verts_[count_][0]=x; verts_[count_][1]=y; ++count_; } }
    void Clear() { count_ = 0; }
    size_t Count() const { return count_; }
    const float* Vertex(size_t i) const { return verts_[i]; }

    bool IsEmpty() const { return count_ == 0; }

    // Total arc length (sum of segment lengths).
    float Length() const {
        if (count_ < 2) return 0;
        float total = 0;
        for (size_t i = 0; i+1 < count_; ++i) total += SegLength(i);
        return total;
    }

    // Point at a given arc-length distance from the start.
    bool PointAtDistance(float dist, float& x, float& y) const {
        if (count_ == 0) return false;
        if (count_ == 1) { x=verts_[0][0]; y=verts_[0][1]; return true; }
        if (dist <= 0) { x=verts_[0][0]; y=verts_[0][1]; return true; }
        float walked = 0;
        for (size_t i = 0; i+1 < count_; ++i) {
            float len = SegLength(i);
            if (dist <= walked + len) {
                float u = len > 0 ? (dist-walked)/len : 0;
                x = verts_[i][0] + (verts_[i+1][0]-verts_[i][0])*u;
                y = verts_[i][1] + (verts_[i+1][1]-verts_[i][1])*u;
                return true;
            }
            walked += len;
        }
        x = verts_[count_-1][0]; y = verts_[count_-1][1];
        return true;
    }

    // Closest point on the polyline to a query point.
    float Distance(float px, float py) const {
        if (count_ == 0) return huge();
        if (count_ == 1) return std::sqrt((px-verts_[0][0])*(px-verts_[0][0]) + (py-verts_[0][1])*(py-verts_[0][1]));
        float best = huge();
        for (size_t i = 0; i+1 < count_; ++i) {
            float d = SegPointDistance(i, px, py);
            if (d < best) best = d;
        }
        return best;
    }

private:
    static constexpr size_t capacity = 64;
    float verts_[capacity][2] = {};
    size_t count_ = 0;

    static float huge() { return 1e30f; }
    float SegLength(size_t i) const {
        float dx = verts_[i+1][0]-verts_[i][0];
        float dy = verts_[i+1][1]-verts_[i][1];
        return std::sqrt(dx*dx+dy*dy);
    }
    float SegPointDistance(size_t i, float px, float py) const {
        float ax = verts_[i][0], ay = verts_[i][1];
        float bx = verts_[i+1][0], by = verts_[i+1][1];
        float dx = bx-ax, dy = by-ay;
        float len2 = dx*dx+dy*dy;
        if (len2 <= 0) return std::sqrt((px-ax)*(px-ax)+(py-ay)*(py-ay));
        float t = ((px-ax)*dx + (py-ay)*dy)/len2;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float cx = ax+dx*t, cy = ay+dy*t;
        return std::sqrt((px-cx)*(px-cx)+(py-cy)*(py-cy));
    }
};

} // namespace bighero
