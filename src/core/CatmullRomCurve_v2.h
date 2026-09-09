#pragma once
#include <cstdint>
#include <vector>
#include <array>

namespace bighero {

// CatmullRomCurve: a Catmull-Rom spline passing through a list of control
// points. Interpolates smoothly between points, using neighboring points to
// compute tangents. Self-contained, std-lib only.
class CatmullRomCurve {
public:
    CatmullRomCurve() = default;

    void Clear() { points_.clear(); }
    bool IsEmpty() const { return points_.empty(); }
    size_t PointCount() const { return points_.size(); }

    void AddPoint(float x, float y, float z) { points_.push_back({x, y, z}); }
    void SetPoint(size_t i, float x, float y, float z) {
        if (i < points_.size()) points_[i] = {x, y, z};
    }
    const float* Point(size_t i) const { return points_[i].data(); }

    // Evaluate at parameter t in [0,1] across the whole curve. t=0 is the first
    // point, t=1 the last. Writes position to out[0..2].
    void Evaluate(float t, float* out) const {
        if (points_.empty()) { out[0]=out[1]=out[2]=0; return; }
        if (points_.size() == 1) { out[0]=points_[0][0]; out[1]=points_[0][1]; out[2]=points_[0][2]; return; }
        if (t <= 0) { out[0]=points_[0][0]; out[1]=points_[0][1]; out[2]=points_[0][2]; return; }
        if (t >= 1) { size_t n=points_.size()-1; out[0]=points_[n][0]; out[1]=points_[n][1]; out[2]=points_[n][2]; return; }

        size_t n = points_.size() - 1;
        float seg = t * (float)n;
        size_t i = (size_t)seg;
        if (i >= n) i = n - 1;
        float u = seg - (float)i;

        const float* p0 = (i == 0) ? points_[0].data() : points_[i-1].data();
        const float* p1 = points_[i].data();
        const float* p2 = points_[i+1].data();
        const float* p3 = (i+2 < points_.size()) ? points_[i+2].data() : points_[i+1].data();

        // Standard Catmull-Rom basis.
        float u2 = u*u, u3 = u2*u;
        for (int c = 0; c < 3; ++c) {
            float v = 0.5f * (
                (2.0f*p1[c]) +
                (-p0[c] + p2[c]) * u +
                (2.0f*p0[c] - 5.0f*p1[c] + 4.0f*p2[c] - p3[c]) * u2 +
                (-p0[c] + 3.0f*p1[c] - 3.0f*p2[c] + p3[c]) * u3
            );
            out[c] = v;
        }
    }

    // Sample N+1 uniformly spaced points into out (size 3*count).
    void Sample(size_t count, std::vector<float>& out) const {
        out.resize(count * 3);
        if (count == 0) return;
        for (size_t k = 0; k < count; ++k) {
            float t = (count == 1) ? 0 : (float)k / (float)(count-1);
            Evaluate(t, &out[k*3]);
        }
    }

private:
    std::vector<std::array<float,3>> points_;
};

} // namespace bighero
