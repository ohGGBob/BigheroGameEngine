#pragma once
#include <cmath>

namespace bighero {

// 2D affine transform as a 3x2 matrix (rotation, scale, translation).
struct Matrix3x2 {
    // Row-major: [m00 m01 m02; m10 m11 m12], where m02/m12 are translation.
    float m[6];

    Matrix3x2() { m[0]=1; m[1]=0; m[2]=0; m[3]=0; m[4]=1; m[5]=0; }

    static Matrix3x2 Identity() {
        Matrix3x2 r;
        r.m[0]=1; r.m[1]=0; r.m[2]=0;
        r.m[3]=0; r.m[4]=1; r.m[5]=0;
        return r;
    }

    static Matrix3x2 Translation(float tx, float ty) {
        Matrix3x2 r; r.m[2]=tx; r.m[5]=ty; return r;
    }
    static Matrix3x2 Rotation(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        Matrix3x2 r;
        r.m[0]=c; r.m[1]=-s;
        r.m[3]=s; r.m[4]=c;
        return r;
    }
    static Matrix3x2 Scale(float sx, float sy) {
        Matrix3x2 r; r.m[0]=sx; r.m[4]=sy; return r;
    }

    void SetIdentity() { *this = Identity(); }

    // Transform a 2D point (affine).
    void TransformPoint(float px, float py, float& ox, float& oy) const {
        ox = m[0]*px + m[1]*py + m[2];
        oy = m[3]*px + m[4]*py + m[5];
    }

    // Transform a 2D direction (no translation).
    void TransformVector(float vx, float vy, float& ox, float& oy) const {
        ox = m[0]*vx + m[1]*vy;
        oy = m[3]*vx + m[4]*vy;
    }

    // Multiplication: this * other (apply other first, then this).
    Matrix3x2 operator*(const Matrix3x2& o) const {
        Matrix3x2 r;
        r.m[0] = m[0]*o.m[0] + m[1]*o.m[3];
        r.m[1] = m[0]*o.m[1] + m[1]*o.m[4];
        r.m[2] = m[0]*o.m[2] + m[1]*o.m[5] + m[2];
        r.m[3] = m[3]*o.m[0] + m[4]*o.m[3];
        r.m[4] = m[3]*o.m[1] + m[4]*o.m[4];
        r.m[5] = m[3]*o.m[2] + m[4]*o.m[5] + m[5];
        return r;
    }

    static Matrix3x2 TRS(float tx, float ty, float rad, float sx, float sy) {
        Matrix3x2 t = Translation(tx, ty);
        Matrix3x2 r = Rotation(rad);
        Matrix3x2 s = Scale(sx, sy);
        return t * r * s;
    }

    float Determinant() const { return m[0]*m[4] - m[1]*m[3]; }

    // Inverse; returns false if singular.
    bool Inverse(Matrix3x2& out) const {
        float d = Determinant();
        if (std::fabs(d) < 1e-12f) return false;
        float inv = 1.0f / d;
        out.m[0] =  m[4] * inv;
        out.m[1] = -m[1] * inv;
        out.m[3] = -m[3] * inv;
        out.m[4] =  m[0] * inv;
        out.m[2] = -(out.m[0]*m[2] + out.m[1]*m[5]);
        out.m[5] = -(out.m[3]*m[2] + out.m[4]*m[5]);
        return true;
    }
};

} // namespace bighero
