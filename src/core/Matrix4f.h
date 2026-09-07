#pragma once
#include <cmath>
#include <cstring>

namespace bighero {

// 4x4 row-major float matrix used for 3D transforms and projections.
class Matrix4f {
public:
    float m[4][4];

    Matrix4f() {
        std::memset(&m, 0, sizeof(m));
        m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
    }

    static Matrix4f Identity() {
        return Matrix4f();
    }

    static Matrix4f Zero() {
        Matrix4f r;
        std::memset(&r.m, 0, sizeof(r.m));
        return r;
    }

    static Matrix4f Translation(float tx, float ty, float tz) {
        Matrix4f r = Identity();
        r.m[0][3] = tx; r.m[1][3] = ty; r.m[2][3] = tz;
        return r;
    }

    static Matrix4f Scale(float sx, float sy, float sz) {
        Matrix4f r = Identity();
        r.m[0][0] = sx; r.m[1][1] = sy; r.m[2][2] = sz;
        return r;
    }

    static Matrix4f RotationX(float a) {
        Matrix4f r = Identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[1][1] = c; r.m[1][2] = -s;
        r.m[2][1] = s; r.m[2][2] = c;
        return r;
    }

    static Matrix4f RotationY(float a) {
        Matrix4f r = Identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[0][0] = c; r.m[0][2] = s;
        r.m[2][0] = -s; r.m[2][2] = c;
        return r;
    }

    static Matrix4f RotationZ(float a) {
        Matrix4f r = Identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[0][0] = c; r.m[0][1] = -s;
        r.m[1][0] = s; r.m[1][1] = c;
        return r;
    }

    Matrix4f operator*(const Matrix4f& o) const {
        Matrix4f r = Zero();
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 4; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }

    void TransformPoint(float& x, float& y, float& z) const {
        float nx = m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3];
        float ny = m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3];
        float nz = m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3];
        x = nx; y = ny; z = nz;
    }

    static Matrix4f Perspective(float fovYRadians, float aspect, float znear, float zfar) {
        Matrix4f r = Zero();
        float f = 1.0f / std::tan(fovYRadians * 0.5f);
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = (zfar + znear) / (znear - zfar);
        r.m[2][3] = (2.0f * zfar * znear) / (znear - zfar);
        r.m[3][2] = -1.0f;
        return r;
    }

    static Matrix4f Orthographic(float l, float r_, float b, float t, float n, float f) {
        (void)n;
        Matrix4f m4 = Identity();
        m4.m[0][0] = 2.0f / (r_ - l);
        m4.m[1][1] = 2.0f / (t - b);
        m4.m[2][2] = -2.0f / (f - n);
        m4.m[0][3] = -(r_ + l) / (r_ - l);
        m4.m[1][3] = -(t + b) / (t - b);
        m4.m[2][3] = -(f + n) / (f - n);
        return m4;
    }

    Matrix4f Transposed() const {
        Matrix4f r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                r.m[i][j] = m[j][i];
        return r;
    }
};

} // namespace bighero
