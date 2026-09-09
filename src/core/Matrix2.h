#pragma once
#include <cmath>

namespace bighero {

// Matrix2: a 2x2 column-major float matrix with multiply, inverse,
// determinant, and transform helpers. Self-contained, std-lib only.
struct Matrix2 {
    float m00, m01, m10, m11;

    Matrix2() : m00(1), m01(0), m10(0), m11(1) {}
    Matrix2(float a, float b, float c, float d) : m00(a), m01(b), m10(c), m11(d) {}

    static Matrix2 Identity() { return Matrix2(1, 0, 0, 1); }

    Matrix2 operator*(const Matrix2& o) const {
        return Matrix2(
            m00 * o.m00 + m01 * o.m10, m00 * o.m01 + m01 * o.m11,
            m10 * o.m00 + m11 * o.m10, m10 * o.m01 + m11 * o.m11);
    }
    float Determinant() const { return m00 * m11 - m01 * m10; }
    bool Invert(Matrix2& out) const {
        float d = Determinant();
        if (std::fabs(d) < 1e-12f) return false;
        float inv = 1.0f / d;
        out = Matrix2(m11 * inv, -m01 * inv, -m10 * inv, m00 * inv);
        return true;
    }
    // Transform a 2D point (assumes translation outside).
    float TransformX(float x, float y) const { return m00 * x + m01 * y; }
    float TransformY(float x, float y) const { return m10 * x + m11 * y; }
};

} // namespace bighero
