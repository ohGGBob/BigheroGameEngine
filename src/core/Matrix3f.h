#pragma once
#include <cmath>
#include <cstring>
#include <algorithm>

namespace bighero {

// 3x3 row-major float matrix. Pure standard-library math helper.
class Matrix3f {
public:
    float m[3][3];

    Matrix3f() {
        std::memset(&m, 0, sizeof(m));
        m[0][0] = m[1][1] = m[2][2] = 1.0f;
    }

    static Matrix3f Identity() {
        return Matrix3f();
    }

    static Matrix3f Zero() {
        Matrix3f r;
        std::memset(&r.m, 0, sizeof(r.m));
        return r;
    }

    static Matrix3f Scale(float sx, float sy) {
        Matrix3f r = Identity();
        r.m[0][0] = sx;
        r.m[1][1] = sy;
        return r;
    }

    static Matrix3f Rotation(float radians) {
        Matrix3f r;
        float c = std::cos(radians);
        float s = std::sin(radians);
        std::memset(&r.m, 0, sizeof(r.m));
        r.m[0][0] = c; r.m[0][1] = -s;
        r.m[1][0] = s; r.m[1][1] = c;
        r.m[2][2] = 1.0f;
        return r;
    }

    static Matrix3f Translation(float tx, float ty) {
        Matrix3f r = Identity();
        r.m[0][2] = tx;
        r.m[1][2] = ty;
        return r;
    }

    Matrix3f operator*(const Matrix3f& o) const {
        Matrix3f r = Zero();
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }

    void TransformPoint(float& x, float& y) const {
        float nx = m[0][0] * x + m[0][1] * y + m[0][2];
        float ny = m[1][0] * x + m[1][1] * y + m[1][2];
        x = nx; y = ny;
    }

    Matrix3f Transposed() const {
        Matrix3f r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[j][i];
        return r;
    }

    float Determinant() const {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
             - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }
};

} // namespace bighero
