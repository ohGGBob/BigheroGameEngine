#pragma once
#include <cmath>

namespace bighero {

// CubicHermite: a compact unified cubic-Hermite basis evaluation used for
// keyframe interpolation between two endpoint values with explicit tangents.
class CubicHermite {
public:
    CubicHermite() = default;

    // Build tangents from finite differences (Catmull-Rom style).
    static float TangentFor(float prev, float next, float dt) {
        if (dt <= 0) return 0;
        return (next - prev) / (2.0f * dt);
    }
    // Evaluate the Hermite basis with clamped tangent.
    static float Eval(float p0, float m0, float p1, float m1, float t) {
        float t2 = t * t, t3 = t2 * t;
        float h00 = 2*t3 - 3*t2 + 1;
        float h10 = t3 - 2*t2 + t;
        float h01 = -2*t3 + 3*t2;
        float h11 = t3 - t2;
        return h00*p0 + h10*m0 + h01*p1 + h11*m1;
    }
    // Recompute tangents from a closed loop of values and evaluate at t
    // in [0, n-1] where n = number of samples.
    static float EvaluateLoop(const float* values, int n, float t, int stride = 1) {
        if (n <= 0) return 0;
        if (n == 1) return values[0];
        if (t <= 0) t = 0;
        float maxT = (float)(n - 1);
        if (t >= maxT) t = maxT;
        int i = (int)t;
        if (i >= n - 1) i = n - 2;
        float local = t - i;
        float p0 = values[i * stride];
        float p1 = values[(i + 1) * stride];
        float prev = values[(i - 1 + n) % n * stride];
        float next = values[(i + 2) % n * stride];
        float m0 = TangentFor(prev, p1, 1.0f);
        float m1 = TangentFor(p0, next, 1.0f);
        return Eval(p0, m0, p1, m1, local);
    }
};

} // namespace bighero
