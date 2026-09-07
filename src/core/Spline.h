#pragma once
#include <vector>
#include <cmath>

namespace bighero {

// Catmull-Rom spline through a list of control points, sampled in 2D.
class Spline2D {
public:
    void Clear() { xs_.clear(); ys_.clear(); }
    std::size_t Count() const { return xs_.size(); }
    bool Empty() const { return xs_.empty(); }

    void AddPoint(float x, float y) { xs_.push_back(x); ys_.push_back(y); }

    // Sample the spline at global parameter u in [0, 1].
    void Sample(float u, float& outX, float& outY) const {
        std::size_t n = xs_.size();
        if (n == 0) { outX = outY = 0; return; }
        if (n == 1) { outX = xs_[0]; outY = ys_[0]; return; }
        if (u <= 0.0f) { outX = xs_[0]; outY = ys_[0]; return; }
        if (u >= 1.0f) { outX = xs_[n-1]; outY = ys_[n-1]; return; }

        float seg = u * (float)(n - 1);
        int i0 = (int)seg;
        float t = seg - (float)i0;
        int i1 = i0 + 1;
        int im1 = (i0 - 1 < 0) ? 0 : i0 - 1;
        int i2 = (i1 + 1 > (int)n - 1) ? (int)n - 1 : i1 + 1;

        outX = Catmull(xs_[im1], xs_[i0], xs_[i1], xs_[i2], t);
        outY = Catmull(ys_[im1], ys_[i0], ys_[i1], ys_[i2], t);
    }

private:
    static float Catmull(float p0, float p1, float p2, float p3, float t) {
        float t2 = t * t;
        float t3 = t2 * t;
        return 0.5f * ((2.0f * p1) +
                       (-p0 + p2) * t +
                       (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                       (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    }

    std::vector<float> xs_, ys_;
};

} // namespace bighero
