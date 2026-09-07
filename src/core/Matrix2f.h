#pragma once
#include <cmath>
#include <cstring>

namespace bighero {

// 2x2 row-major float matrix.
class Matrix2f {
public:
    float m[2][2];

    Matrix2f() {
        std::memset(&m, 0, sizeof(m));
        m[0][0] = m[1][1] = 1.0f;
    }

    static Matrix2f Identity() { return Matrix2f(); }

    static Matrix2f Zero() {
        Matrix2f r;
        std::memset(&r.m, 0, sizeof(r.m));
        return r;
    }

    static Matrix2f Rotation(float radians) {
        Matrix2f r;
        float c = std::cos(radians), s = std::sin(radians);
        std::memset(&r.m, 0, sizeof(r.m));
        r.m[0][0] = c; r.m[0][1] = -s;
        r.m[1][0] = s; r.m[1][1] = c;
        return r;
    }

    Matrix2f operator*(const Matrix2f& o) const {
        Matrix2f r = Zero();
        for (int i = 0; i < 2; ++i)
            for (int j = 0; j < 2; ++j)
                for (int k = 0; k < 2; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }

    void TransformPoint(float& x, float& y) const {
        float nx = m[0][0] * x + m[0][1] * y;
        float ny = m[1][0] * x + m[1][1] * y;
        x = nx; y = ny;
    }

    float Determinant() const { return m[0][0] * m[1][1] - m[0][1] * m[1][0]; }
};

} // namespace bighero
