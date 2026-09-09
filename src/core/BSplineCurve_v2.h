#pragma once
#include <cstdint>
#include <vector>
#include <array>

namespace bighero {

// BSplineCurve: a uniform cubic B-spline through a set of control points. It
// approximates (does not interpolate) the control polygon with C2 continuity.
// Self-contained, std-lib only.
class BSplineCurve {
public:
    BSplineCurve() = default;
    void Clear() { points_.clear(); }
    bool IsEmpty() const { return points_.empty(); }
    size_t ControlPointCount() const { return points_.size(); }

    void AddPoint(float x, float y, float z) { points_.push_back({x,y,z}); }
    const float* Point(size_t i) const { return points_[i].data(); }

    // Evaluate a single uniform cubic B-spline segment given 4 control points
    // at parameter u in [0,1].
    static void EvalSegment(const float* p0, const float* p1, const float* p2,
                            const float* p3, float u, float* out) {
        float u2 = u*u, u3 = u2*u;
        for (int c = 0; c < 3; ++c) {
            out[c] = (1.0f/6.0f) * (
                (-p0[c] + 3*p1[c] - 3*p2[c] + p3[c]) * u3 +
                (3*p0[c] - 6*p1[c] + 3*p2[c]) * u2 +
                (-3*p0[c] + 3*p2[c]) * u +
                (p0[c] + 4*p1[c] + p2[c])
            );
        }
    }

    // Evaluate at global parameter t in [0,1] across all control points.
    void Evaluate(float t, float* out) const {
        size_t n = points_.size();
        if (n == 0) { out[0]=out[1]=out[2]=0; return; }
        if (n == 1) { out[0]=points_[0][0]; out[1]=points_[0][1]; out[2]=points_[0][2]; return; }
        if (n == 2) { // linear interpolate
            float u = t;
            for (int c=0;c<3;++c) out[c] = points_[0][c]*(1-u) + points_[1][c]*u;
            return;
        }
        if (t <= 0) { out[0]=points_[0][0]; out[1]=points_[0][1]; out[2]=points_[0][2]; return; }
        if (t >= 1) { out[0]=points_[n-1][0]; out[1]=points_[n-1][1]; out[2]=points_[n-1][2]; return; }

        size_t segCount = n - 3;
        float seg = t * (float)(segCount);
        size_t i = (size_t)seg;
        if (i >= segCount) i = segCount - 1;
        float u = seg - (float)i;
        EvalSegment(points_[i].data(), points_[i+1].data(), points_[i+2].data(), points_[i+3].data(), u, out);
    }

    void Sample(size_t count, std::vector<float>& out) const {
        out.resize(count*3);
        if (count == 0) return;
        for (size_t k = 0; k < count; ++k) {
            float t = (count==1)?0:(float)k/(float)(count-1);
            Evaluate(t, &out[k*3]);
        }
    }

private:
    std::vector<std::array<float,3>> points_;
};

} // namespace bighero
