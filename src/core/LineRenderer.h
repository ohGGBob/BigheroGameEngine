#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// LineRenderer: a list of 3D line points forming a polyline, with width,
// color, and segment count. Provides point and length helpers. Pure data.
class LineRenderer {
public:
    struct Point { float x, y, z; };

    LineRenderer() {}

    void Clear() { pts_.clear(); }
    void AddPoint(float x, float y, float z) { pts_.push_back({x,y,z}); }
    void SetPoints(const Point* p, std::size_t n) {
        pts_.assign(p, p + n);
    }
    std::size_t Count() const { return pts_.size(); }
    bool Get(std::size_t i, float& x, float& y, float& z) const {
        if (i >= pts_.size()) return false;
        x = pts_[i].x; y = pts_[i].y; z = pts_[i].z;
        return true;
    }

    void SetWidth(float w) { width_ = w < 0 ? 0 : w; }
    float Width() const { return width_; }
    void SetColor(float r, float g, float b, float a = 1.0f) {
        r_=r; g_=g; b_=b; a_=a;
    }
    void Color(float& r, float& g, float& b, float& a) const { r=r_; g=g_; b=b_; a=a_; }
    void SetLoop(bool l) { loop_ = l; }
    bool Loop() const { return loop_; }

    float Length() const {
        float len = 0;
        std::size_t n = pts_.size();
        if (n == 0) return 0;
        for (std::size_t i = 1; i < n; ++i)
            len += Dist(pts_[i-1], pts_[i]);
        if (loop_ && n > 2) len += Dist(pts_[n-1], pts_[0]);
        return len;
    }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    static float Dist(const Point& a, const Point& b) {
        float dx=b.x-a.x, dy=b.y-a.y, dz=b.z-a.z;
        return std::sqrt(dx*dx+dy*dy+dz*dz);
    }
    std::vector<Point> pts_;
    float width_ = 1.0f;
    float r_=1, g_=1, b_=1, a_=1;
    bool loop_ = false;
    bool enabled_ = true;
};

} // namespace bighero
