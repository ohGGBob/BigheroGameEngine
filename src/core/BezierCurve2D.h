#pragma once
#include <vector>
#include <cmath>

namespace bighero {

// 2D Bezier curve utilities (quadratic & cubic) with sampling/tangent.
class BezierCurve2D {
public:
    struct Pt { float x, y; };

    // Quadratic Bezier with 3 control points.
    static Pt Quadratic(const Pt& p0, const Pt& p1, const Pt& p2, float t) {
        float mt = 1 - t;
        float a = mt*mt, b = 2*mt*t, c = t*t;
        return { a*p0.x + b*p1.x + c*p2.x,
                 a*p0.y + b*p1.y + c*p2.y };
    }

    // Cubic Bezier with 4 control points.
    static Pt Cubic(const Pt& p0, const Pt& p1, const Pt& p2, const Pt& p3, float t) {
        float mt = 1 - t;
        float a = mt*mt*mt, b = 3*mt*mt*t, c = 3*mt*t*t, d = t*t*t;
        return { a*p0.x + b*p1.x + c*p2.x + d*p3.x,
                 a*p0.y + b*p1.y + c*p2.y + d*p3.y };
    }

    // Tangent of a cubic curve at t (derivative).
    static Pt CubicTangent(const Pt& p0, const Pt& p1, const Pt& p2, const Pt& p3, float t) {
        float mt = 1 - t;
        float a = 3*mt*mt, b = 6*mt*t, c = 3*t*t;
        return { a*(p1.x-p0.x) + b*(p2.x-p1.x) + c*(p3.x-p2.x),
                 a*(p1.y-p0.y) + b*(p2.y-p1.y) + c*(p3.y-p2.y) };
    }

    // Sample a cubic Bezier into N+1 points (N segments).
    static std::vector<Pt> SampleCubic(const Pt& p0, const Pt& p1, const Pt& p2, const Pt& p3, int N) {
        std::vector<Pt> out;
        if (N < 1) N = 1;
        for (int i = 0; i <= N; ++i) out.push_back(Cubic(p0,p1,p2,p3, (float)i/N));
        return out;
    }
};

} // namespace bighero
