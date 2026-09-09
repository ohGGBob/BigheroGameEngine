#pragma once
#include <cmath>

namespace bighero {

// Matrix3: a 3x3 column-major float matrix with multiply, determinant,
// inverse, and affine transform helpers (rotation/scale/translation).
struct Matrix3 {
    float m[9];

    Matrix3() {
        m[0]=1; m[1]=0; m[2]=0;
        m[3]=0; m[4]=1; m[5]=0;
        m[6]=0; m[7]=0; m[8]=1;
    }
    Matrix3(float a00,float a01,float a02,
            float a10,float a11,float a12,
            float a20,float a21,float a22) {
        m[0]=a00; m[1]=a01; m[2]=a02;
        m[3]=a10; m[4]=a11; m[5]=a12;
        m[6]=a20; m[7]=a21; m[8]=a22;
    }

    static Matrix3 Identity() { return Matrix3(); }

    Matrix3 operator*(const Matrix3& o) const {
        Matrix3 r;
        for (int c = 0; c < 3; ++c)
            for (int rr = 0; rr < 3; ++rr) {
                float s = 0;
                for (int k = 0; k < 3; ++k) s += m[k*3+rr] * o.m[c*3+k];
                r.m[c*3+rr] = s;
            }
        return r;
    }
    float Determinant() const {
        return m[0]*(m[4]*m[8]-m[5]*m[7])
             - m[3]*(m[1]*m[8]-m[2]*m[7])
             + m[6]*(m[1]*m[5]-m[2]*m[4]);
    }
    void TransformPoint(float x, float y, float& ox, float& oy) const {
        ox = m[0]*x + m[1]*y + m[2];
        oy = m[3]*x + m[4]*y + m[5];
    }
    static Matrix3 RotationZ(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return Matrix3(c, -s, 0, s, c, 0, 0, 0, 1);
    }
    static Matrix3 Scale(float sx, float sy) {
        return Matrix3(sx, 0, 0, 0, sy, 0, 0, 0, 1);
    }
    static Matrix3 Translation(float tx, float ty) {
        return Matrix3(1, 0, tx, 0, 1, ty, 0, 0, 1);
    }
};

} // namespace bighero
