#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// Curve2D: a 2D parametric curve sampled from a control-point polyline with
// Catmull-Rom interpolation. Pure CPU-side math + sampling helpers.
class Curve2D {
public:
    struct Point { float x, y; };

    Curve2D() {}

    void AddPoint(float x, float y) { points_.push_back({x, y}); }
    std::size_t PointCount() const { return points_.size(); }
    bool GetPoint(std::size_t i, Point& out) const {
        if (i >= points_.size()) return false;
        out = points_[i]; return true;
    }
    void Clear() { points_.clear(); }
    bool IsClosed() const { return closed_; }
    void SetClosed(bool c) { closed_ = c; }

    // Catmull-Rom sample at parameter t in [0, 1].
    Point Sample(float t) const {
        if (points_.empty()) return {0, 0};
        if (points_.size() == 1) return points_[0];
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float seg = t * (float)(points_.size() - 1);
        std::size_t i = (std::size_t)seg;
        if (i >= points_.size() - 1) return points_.back();
        float f = seg - (float)i;
        std::size_t p0 = (i == 0) ? i : i - 1;
        std::size_t p1 = i, p2 = i + 1;
        std::size_t p3 = (i + 2 < points_.size()) ? i + 2 : i + 1;
        return CatmullRom(points_[p0], points_[p1], points_[p2], points_[p3], f);
    }

    // Approximate total arc length with N samples of the polyline.
    float ApproximateLength(std::size_t samples = 64) const {
        if (points_.size() < 2) return 0.0f;
        float len = 0.0f; Point prev = Sample(0.0f);
        for (std::size_t s = 1; s <= samples; ++s) {
            Point cur = Sample((float)s / (float)samples);
            float dx = cur.x - prev.x, dy = cur.y - prev.y;
            len += std::sqrt(dx*dx + dy*dy);
            prev = cur;
        }
        return len;
    }

private:
    static Point CatmullRom(const Point& p0, const Point& p1,
                            const Point& p2, const Point& p3, float t) {
        float t2 = t*t, t3 = t2*t;
        float x = 0.5f * ((2*p1.x) + (-p0.x+p2.x)*t +
                         (2*p0.x-5*p1.x+4*p2.x-p3.x)*t2 +
                         (-p0.x+3*p1.x-3*p2.x+p3.x)*t3);
        float y = 0.5f * ((2*p1.y) + (-p0.y+p2.y)*t +
                         (2*p0.y-5*p1.y+4*p2.y-p3.y)*t2 +
                         (-p0.y+3*p1.y-3*p2.y+p3.y)*t3);
        return {x, y};
    }
    std::vector<Point> points_;
    bool closed_ = false;
};

} // namespace bighero
