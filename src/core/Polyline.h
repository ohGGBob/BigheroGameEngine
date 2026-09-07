#pragma once
#include <vector>
#include <cmath>

namespace bighero {

// A sequence of connected 2D segments with length and point access.
class Polyline {
public:
    std::vector<std::pair<float,float>> pts;

    Polyline() {}
    explicit Polyline(std::vector<std::pair<float,float>> points) : pts(std::move(points)) {}

    void AddPoint(float x, float y) { pts.push_back({x, y}); }
    void Clear() { pts.clear(); }
    bool Empty() const { return pts.empty(); }
    std::size_t Size() const { return pts.size(); }

    // Total arc length.
    float Length() const {
        float len = 0;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i)
            len += Dist(pts[i], pts[i+1]);
        return len;
    }

    // Segment length from index i to i+1.
    float SegmentLength(std::size_t i) const {
        if (i + 1 >= pts.size()) return 0;
        return Dist(pts[i], pts[i+1]);
    }

    // Give the point at arc-length distance t along the polyline (clamped).
    std::pair<float,float> PointAt(float t) const {
        if (pts.empty()) return {0,0};
        if (pts.size() == 1) return pts[0];
        float total = Length();
        if (total <= 0) return pts[0];
        if (t < 0) t = 0; if (t > total) t = total;
        float acc = 0;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            float seg = Dist(pts[i], pts[i+1]);
            if (seg <= 0) continue;
            if (acc + seg >= t) {
                float f = (t - acc) / seg;
                return { pts[i].first + (pts[i+1].first - pts[i].first) * f,
                         pts[i].second + (pts[i+1].second - pts[i].second) * f };
            }
            acc += seg;
        }
        return pts.back();
    }

    void Reverse() { if (!pts.empty()) std::reverse(pts.begin(), pts.end()); }

private:
    static float Dist(const std::pair<float,float>& a, const std::pair<float,float>& b) {
        float dx = a.first - b.first, dy = a.second - b.second;
        return std::sqrt(dx*dx + dy*dy);
    }
};

} // namespace bighero
