#pragma once
#include <cmath>

namespace bighero {

// BezierCurve: evaluation of quadratic/cubic Bezier curves with derivative
// and arc-length sampling helpers. Standard-library only, self-contained.
class BezierCurve {
public:
    BezierCurve() = default;

    // Quadratic Bezier point at t.
    static void EvaluateQuadratic(float p0x, float p0y, float p1x, float p1y,
                                  float p2x, float p2y, float t,
                                  float& ox, float& oy) {
        float mt = 1 - t;
        float a = mt * mt, b = 2 * mt * t, c = t * t;
        ox = a * p0x + b * p1x + c * p2x;
        oy = a * p0y + b * p1y + c * p2y;
    }
    // Cubic Bezier point at t.
    static void EvaluateCubic(float p0x, float p0y, float p1x, float p1y,
                              float p2x, float p2y, float p3x, float p3y,
                              float t, float& ox, float& oy) {
        float mt = 1 - t;
        float a = mt * mt * mt, b = 3 * mt * mt * t, c = 3 * mt * t * t, d = t * t * t;
        ox = a * p0x + b * p1x + c * p2x + d * p3x;
        oy = a * p0y + b * p1y + c * p2y + d * p3y;
    }
    // Cubic Bezier derivative (tangent) at t.
    static void DerivativeCubic(float p0x, float p0y, float p1x, float p1y,
                                float p2x, float p2y, float p3x, float p3y,
                                float t, float& dx, float& dy) {
        float mt = 1 - t;
        float a = 3 * mt * mt, b = 6 * mt * t, c = 3 * t * t;
        dx = a * (p1x - p0x) + b * (p2x - p1x) + c * (p3x - p2x);
        dy = a * (p1y - p0y) + b * (p2y - p1y) + c * (p3y - p2y);
    }
    // Subdivide a cubic Bezier at t into two segments (left, right).
    static void SubdivideCubic(float p0x, float p0y, float p1x, float p1y,
                               float p2x, float p2y, float p3x, float p3y,
                               float t, float out[8]) {
        float mt = 1 - t;
        float q0x = p0x * mt + p1x * t, q0y = p0y * mt + p1y * t;
        float q1x = p1x * mt + p2x * t, q1y = p1y * mt + p2y * t;
        float q2x = p2x * mt + p3x * t, q2y = p2y * mt + p3y * t;
        float r0x = q0x * mt + q1x * t, r0y = q0y * mt + q1y * t;
        float r1x = q1x * mt + q2x * t, r1y = q1y * mt + q2y * t;
        float s0x = r0x * mt + r1x * t, s0y = r0y * mt + r1y * t;
        out[0]=p0x; out[1]=p0y; out[2]=q0x; out[3]=q0y;
        out[4]=r0x; out[5]=r0y; out[6]=s0x; out[7]=s0y;
    }
};

} // namespace bighero
