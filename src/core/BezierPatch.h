#pragma once
#include <vector>
#include <cmath>

namespace bighero {

// Evaluates one or more Bezier patches (bicubic patch = 4x4 control points).
class BezierPatch {
public:
    struct Vec3 { float x, y, z; };

    // Evaluate a single cubic Bezier patch at (u,v) in [0,1].
    // control is a 4x4 grid of points: row-major (row = v direction).
    static Vec3 Evaluate(const Vec3 control[4][4], float u, float v) {
        // de Casteljau along one axis then the other
        Vec3 rows[4];
        for (int i = 0; i < 4; ++i) rows[i] = EvalCubic(control[i], u);
        Vec3 ctrl[4] = { rows[0], rows[1], rows[2], rows[3] };
        return EvalCubic(ctrl, v);
    }

    // Cubic Bezier curve eval with 4 control points.
    static Vec3 EvaluateCurve(const Vec3 p[4], float t) {
        float mt = 1 - t;
        float a = mt*mt*mt, b = 3*mt*mt*t, c = 3*mt*t*t, d = t*t*t;
        return { a*p[0].x + b*p[1].x + c*p[2].x + d*p[3].x,
                 a*p[0].y + b*p[1].y + c*p[2].y + d*p[3].y,
                 a*p[0].z + b*p[1].z + c*p[2].z + d*p[3].z };
    }

    // Tessellate a bicubic patch into a grid of vertices and quad indices.
    static void Tessellate(std::vector<Vec3>& verts, std::vector<int>& indices,
                           const Vec3 control[4][4], int segs) {
        if (segs < 1) segs = 1;
        verts.clear(); indices.clear();
        for (int j = 0; j <= segs; ++j)
            for (int i = 0; i <= segs; ++i) {
                float u = (float)i / segs, v = (float)j / segs;
                verts.push_back(Evaluate(control, u, v));
            }
        int row = segs + 1;
        for (int j = 0; j < segs; ++j)
            for (int i = 0; i < segs; ++i) {
                int a = j*row + i, b = j*row + i + 1;
                int c = (j+1)*row + i, d = (j+1)*row + i + 1;
                indices.push_back(a); indices.push_back(c); indices.push_back(b);
                indices.push_back(b); indices.push_back(c); indices.push_back(d);
            }
    }

private:
    static Vec3 EvalCubic(const Vec3 p[4], float t) {
        float mt = 1 - t;
        float a = mt*mt*mt, b = 3*mt*mt*t, c = 3*mt*t*t, d = t*t*t;
        return { a*p[0].x + b*p[1].x + c*p[2].x + d*p[3].x,
                 a*p[0].y + b*p[1].y + c*p[2].y + d*p[3].y,
                 a*p[0].z + b*p[1].z + c*p[2].z + d*p[3].z };
    }
};

} // namespace bighero
