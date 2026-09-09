#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// CatmullRomSpline: a centripetal Catmull-Rom spline through a set of control
// points, providing interpolation, evaluation of position/tangent at a global
// parameter, and arc-length unaware indexing. Standard-library only,
// self-contained.
class CatmullRomSpline {
public:
    CatmullRomSpline() {}
    CatmullRomSpline(const std::vector<float>& px, const std::vector<float>& py)
        { SetPoints(px, py); }

    void SetPoints(const std::vector<float>& px, const std::vector<float>& py) {
        pts_.clear();
        std::size_t n = px.size() < py.size() ? px.size() : py.size();
        for (std::size_t i=0;i<n;++i) pts_.push_back({px[i], py[i]});
    }
    void Clear() { pts_.clear(); }
    std::size_t PointCount() const { return pts_.size(); }

    // Evaluate at parameter u in [0,1] across the whole spline (segment-based).
    void Evaluate(float u, float& ox, float& oy) const {
        if (pts_.empty()) { ox=oy=0; return; }
        if (pts_.size() == 1) { ox=pts_[0].x; oy=pts_[0].y; return; }
        if (u <= 0) { ox=pts_.front().x; oy=pts_.front().y; return; }
        if (u >= 1) { ox=pts_.back().x; oy=pts_.back().y; return; }
        std::size_t segCount = pts_.size() - 1;
        float scaled = u * segCount;
        std::size_t seg = (std::size_t)scaled;
        if (seg >= segCount) seg = segCount - 1;
        float t = scaled - seg;
        EvalSegment(seg, t, ox, oy);
    }

    // Tangent (derivative) at u.
    void Tangent(float u, float& tx, float& ty) const {
        if (pts_.size() < 2) { tx=1; ty=0; return; }
        std::size_t segCount = pts_.size() - 1;
        float scaled = u * segCount;
        if (scaled >= segCount) scaled = segCount - 1e-4f;
        if (scaled < 0) scaled = 0;
        std::size_t seg = (std::size_t)scaled;
        Point p0 = Pt(seg), p1 = Pt(seg+1);
        Point pm = Pt(seg-1), pp = Pt(seg+2);
        // Centripetal derivative approx.
        tx = 0.5f*((p1.x-pm.x) + (p1.x-p0.x) - (pp.x-p0.x));
        ty = 0.5f*((p1.y-pm.y) + (p1.y-p0.y) - (pp.y-p0.y));
    }

private:
    struct Point { float x, y; };
    Point Pt(int i) const {
        if (i < 0) return pts_.front();
        if (i >= (int)pts_.size()) return pts_.back();
        return pts_[(std::size_t)i];
    }
    void EvalSegment(std::size_t seg, float t, float& ox, float& oy) const {
        Point p0 = Pt((int)seg-1), p1 = Pt((int)seg), p2 = Pt((int)seg+1), p3 = Pt((int)seg+2);
        float t2 = t*t, t3 = t2*t;
        ox = 0.5f * ((2.0f*p1.x) + (-p0.x+p2.x)*t +
                     (2.0f*p0.x-5.0f*p1.x+4.0f*p2.x-p3.x)*t2 +
                     (-p0.x+3.0f*p1.x-3.0f*p2.x+p3.x)*t3);
        oy = 0.5f * ((2.0f*p1.y) + (-p0.y+p2.y)*t +
                     (2.0f*p0.y-5.0f*p1.y+4.0f*p2.y-p3.y)*t2 +
                     (-p0.y+3.0f*p1.y-3.0f*p2.y+p3.y)*t3);
    }
    std::vector<Point> pts_;
};

} // namespace bighero
