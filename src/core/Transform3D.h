#pragma once
#include <cmath>
#include <cstring>

namespace bighero {

// 3D rigid transform: translation + rotation (as 3x3) + scale.
// Row-major. Composed as M = T * R * S when applied to points.
class Transform3D {
public:
    float tx, ty, tz;
    float r[3][3]; // rotation/scale 3x3

    Transform3D() {
        tx = ty = tz = 0.0f;
        std::memset(&r, 0, sizeof(r));
        r[0][0] = r[1][1] = r[2][2] = 1.0f;
    }

    static Transform3D Identity() { return Transform3D(); }

    static Transform3D Translation(float x, float y, float z) {
        Transform3D t;
        t.tx = x; t.ty = y; t.tz = z;
        return t;
    }

    static Transform3D Scale(float sx, float sy, float sz) {
        Transform3D t;
        t.r[0][0] = sx; t.r[1][1] = sy; t.r[2][2] = sz;
        return t;
    }

    static Transform3D RotationX(float a) {
        Transform3D t;
        float c = std::cos(a), s = std::sin(a);
        std::memset(&t.r, 0, sizeof(t.r));
        t.r[0][0] = 1;
        t.r[1][1] = c; t.r[1][2] = -s;
        t.r[2][1] = s; t.r[2][2] = c;
        return t;
    }

    void SetRotation(const float m[3][3]) {
        std::memcpy(&r, m, sizeof(r));
    }

    void GetRotation(float out[3][3]) const { std::memcpy(out, &r, sizeof(r)); }

    void TransformPoint(const float in[3], float out[3]) const {
        out[0] = r[0][0] * in[0] + r[0][1] * in[1] + r[0][2] * in[2] + tx;
        out[1] = r[1][0] * in[0] + r[1][1] * in[1] + r[1][2] * in[2] + ty;
        out[2] = r[2][0] * in[0] + r[2][1] * in[1] + r[2][2] * in[2] + tz;
    }

    void TransformPoint(float& x, float& y, float& z) const {
        float in[3] = {x, y, z};
        float out[3];
        TransformPoint(in, out);
        x = out[0]; y = out[1]; z = out[2];
    }

    // Inverse-translate-only approximate inverse (no rotation inverse). For rigid transforms.
    Transform3D Inverse() const {
        Transform3D inv;
        // Invert translation using inverse rotation.
        float rt[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                rt[i][j] = r[j][i]; // transpose (orthonormal)
        inv.tx = -(rt[0][0] * tx + rt[0][1] * ty + rt[0][2] * tz);
        inv.ty = -(rt[1][0] * tx + rt[1][1] * ty + rt[1][2] * tz);
        inv.tz = -(rt[2][0] * tx + rt[2][1] * ty + rt[2][2] * tz);
        std::memcpy(&inv.r, &rt, sizeof(rt));
        return inv;
    }
};

} // namespace bighero
