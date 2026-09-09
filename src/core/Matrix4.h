#pragma once
#include <cmath>

namespace bighero {

// Matrix4: a 4x4 column-major float matrix with multiply, determinant,
// perspective/orthographic projection, and transform helpers (affine 3D).
struct Matrix4 {
    float m[16];

    Matrix4() {
        for (int i = 0; i < 16; ++i) m[i] = 0;
        m[0]=1; m[5]=1; m[10]=1; m[15]=1;
    }
    Matrix4(float a00,float a01,float a02,float a03,
            float a10,float a11,float a12,float a13,
            float a20,float a21,float a22,float a23,
            float a30,float a31,float a32,float a33) {
        m[0]=a00; m[1]=a01; m[2]=a02; m[3]=a03;
        m[4]=a10; m[5]=a11; m[6]=a12; m[7]=a13;
        m[8]=a20; m[9]=a21; m[10]=a22; m[11]=a23;
        m[12]=a30; m[13]=a31; m[14]=a32; m[15]=a33;
    }

    static Matrix4 Identity() { return Matrix4(); }

    Matrix4 operator*(const Matrix4& o) const {
        Matrix4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += m[k*4+rr] * o.m[c*4+k];
                r.m[c*4+rr] = s;
            }
        return r;
    }
    // Transform a 3D point (w=1), row-major layout.
    void TransformPoint3D(float x, float y, float z, float& ox, float& oy, float& oz) const {
        float w = m[12]*x + m[13]*y + m[14]*z + m[15];
        ox = (m[0]*x + m[1]*y + m[2]*z + m[3]) / w;
        oy = (m[4]*x + m[5]*y + m[6]*z + m[7]) / w;
        oz = (m[8]*x + m[9]*y + m[10]*z + m[11]) / w;
    }
    static Matrix4 Translation(float tx, float ty, float tz) {
        Matrix4 r;
        r.m[3]=tx; r.m[7]=ty; r.m[11]=tz;
        return r;
    }
    static Matrix4 Scale(float sx, float sy, float sz) {
        Matrix4 r;
        r.m[0]=sx; r.m[5]=sy; r.m[10]=sz;
        return r;
    }
    static Matrix4 RotationX(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return Matrix4(1,0,0,0, 0,c,-s,0, 0,s,c,0, 0,0,0,1);
    }
    static Matrix4 RotationY(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return Matrix4(c,0,s,0, 0,1,0,0, -s,0,c,0, 0,0,0,1);
    }
    static Matrix4 RotationZ(float rad) {
        float c = std::cos(rad), s = std::sin(rad);
        return Matrix4(c,-s,0,0, s,c,0,0, 0,0,1,0, 0,0,0,1);
    }
    // Perspective projection matrix (right-handed, clip z in [0,1]).
    static Matrix4 Perspective(float fovYRad, float aspect, float znear, float zfar) {
        float f = 1.0f / std::tan(fovYRad * 0.5f);
        Matrix4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = 0;
        r.m[0]=f/aspect; r.m[5]=f; r.m[10]=(zfar+znear)/(znear-zfar);
        r.m[11]=-1; r.m[14]=2*zfar*znear/(znear-zfar);
        return r;
    }
    // Orthographic projection matrix.
    static Matrix4 Orthographic(float left, float right, float bottom,
                                float top, float znear, float zfar) {
        Matrix4 r;
        for (int i = 0; i < 16; ++i) r.m[i] = 0;
        r.m[0]=2/(right-left); r.m[5]=2/(top-bottom); r.m[10]=-2/(zfar-znear);
        r.m[12]=-(right+left)/(right-left);
        r.m[13]=-(top+bottom)/(top-bottom);
        r.m[14]=-(zfar+znear)/(zfar-znear);
        r.m[15]=1;
        return r;
    }
};

} // namespace bighero
