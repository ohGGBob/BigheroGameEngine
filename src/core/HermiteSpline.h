#pragma once
#include <cmath>

namespace bighero {

// HermiteSpline: 1D and 2D Hermite (cubic) spline interpolation using
// tangents, useful for keyframed animation and path smoothing.
class HermiteSpline {
public:
    HermiteSpline() = default;

    // Classic cubic Hermite basis functions at t in [0,1].
    static void Basis(float t, float& h00, float& h10, float& h01, float& h11) {
        float t2 = t * t, t3 = t2 * t;
        h00 = 2*t3 - 3*t2 + 1;
        h10 = t3 - 2*t2 + t;
        h01 = -2*t3 + 3*t2;
        h11 = t3 - t2;
    }
    static float Eval(float p0, float m0, float p1, float m1, float t) {
        float h00, h10, h01, h11;
        Basis(t, h00, h10, h01, h11);
        return h00*p0 + h10*m0 + h01*p1 + h11*m1;
    }
    // 2D Hermite evaluation.
    static void Eval2D(float p0x,float p0y,float m0x,float m0y,
                       float p1x,float p1y,float m1x,float m1y,
                       float t, float& ox, float& oy) {
        float h00, h10, h01, h11;
        Basis(t, h00, h10, h01, h11);
        ox = h00*p0x + h10*m0x + h01*p1x + h11*m1x;
        oy = h00*p0y + h10*m0y + h01*p1y + h11*m1y;
    }
    // First derivative of the 1D Hermite spline at t.
    static float Derivative(float p0, float m0, float p1, float m1, float t) {
        float t2 = t * t;
        float dh00 = 6*t2 - 6*t;
        float dh10 = 3*t2 - 4*t + 1;
        float dh01 = -6*t2 + 6*t;
        float dh11 = 3*t2 - 2*t;
        return dh00*p0 + dh10*m0 + dh01*p1 + dh11*m1;
    }
};

} // namespace bighero
