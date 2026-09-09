#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// NURBSCurve: a Non-Uniform Rational B-Spline curve with control points,
// weights and a clamped knot vector. Provides rational evaluation at a global
// parameter. Standard-library only, self-contained.
class NURBSCurve {
public:
    NURBSCurve() {}
    NURBSCurve(std::size_t degree) : degree_(degree) {}

    void SetDegree(std::size_t d) { degree_ = d < 1 ? 1 : d; }
    std::size_t Degree() const { return degree_; }

    void SetControlPoints(const std::vector<float>& px, const std::vector<float>& py) {
        ctrl_.clear();
        std::size_t n = px.size() < py.size() ? px.size() : py.size();
        for (std::size_t i=0;i<n;++i) ctrl_.push_back({px[i], py[i]});
        // Auto-build a clamped knot vector with uniform interpolation.
        BuildKnots();
    }
    void SetWeights(const std::vector<float>& w) {
        weights_ = w;
    }
    std::size_t ControlPointCount() const { return ctrl_.size(); }

    // Evaluate point at u in [0,1] using the De Boor-like rational formula.
    void Evaluate(float u, float& ox, float& oy) const {
        std::size_t n = ctrl_.size();
        if (n == 0) { ox=oy=0; return; }
        if (n == 1) { ox=ctrl_[0].x; oy=ctrl_[0].y; return; }
        if (u <= 0) { ox=ctrl_.front().x; oy=ctrl_.front().y; return; }
        if (u >= 1) { ox=ctrl_.back().x; oy=ctrl_.back().y; return; }
        // Weighted average using the basis functions (approximation).
        float wx=0, wy=0, wsum=0;
        std::size_t m = n;
        for (std::size_t i=0;i<m;++i) {
            float b = Basis(i, degree_, u, knots_);
            float wi = (i < weights_.size() ? weights_[i] : 1.0f);
            wx += b * wi * ctrl_[i].x;
            wy += b * wi * ctrl_[i].y;
            wsum += b * wi;
        }
        if (wsum < 1e-9f) { ox=ctrl_.front().x; oy=ctrl_.front().y; return; }
        ox = wx / wsum; oy = wy / wsum;
    }

    // Approximate tangent at u.
    void Tangent(float u, float& tx, float& ty) const {
        float x0,y0,x1,y1;
        float eps = 1e-3f;
        Evaluate(u, x0,y0);
        float u2 = u+eps > 1.0f ? 1.0f : u+eps;
        Evaluate(u2, x1,y1);
        tx = x1-x0; ty = y1-y0;
    }

private:
    struct Pt { float x, y; };
    // Cox-de Boor recursion for basis N_{i,p}(u) over knots.
    static float Basis(std::size_t i, std::size_t p, float u, const std::vector<float>& knots) {
        if (p == 0) {
            float u0 = knots[i], u1 = knots[i+1];
            return (u >= u0 && u < u1) ? 1.0f : 0.0f;
        }
        float u0=knots[i], u1=knots[i+p], u2=knots[i+1], u3=knots[i+p+1];
        float denom1 = u1-u0, denom2 = u3-u2;
        float left = denom1 > 1e-9f ? (u-u0)/denom1 * Basis(i, p-1, u, knots) : 0.0f;
        float right = denom2 > 1e-9f ? (u3-u)/denom2 * Basis(i+1, p-1, u, knots) : 0.0f;
        return left + right;
    }
    void BuildKnots() {
        std::size_t n = ctrl_.size();
        if (n == 0) return;
        std::size_t p = degree_;
        std::size_t k = n + p + 1;
        knots_.assign(k, 0.0f);
        // Clamped: first p+1 = 0, last p+1 = 1, middle uniform.
        for (std::size_t i=0;i<k;++i) {
            if (i <= p) knots_[i] = 0.0f;
            else if (i >= n) knots_[i] = 1.0f;
            else knots_[i] = (float)(i - p) / (float)(n - p);
        }
    }
    std::vector<Pt> ctrl_;
    std::vector<float> weights_, knots_;
    std::size_t degree_ = 2;
};

} // namespace bighero
