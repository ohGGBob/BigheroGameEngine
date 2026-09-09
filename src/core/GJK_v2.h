#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// GJK: a correct 2D Gilbert-Johnson-Keerthi intersection test. Shapes are
// convex; a support function maps a unit direction to the farthest point.
// The simplex is reduced (point -> segment -> triangle) each iteration to find
// whether the origin is inside the Minkowski difference A ⊖ B.
// Self-contained, std-lib only.
//
// Support signature:
//   void support(const void* data, const float* dir, float* outPoint); // 2D
class GJK {
public:
    GJK() = default;

    // Run the GJK overlap test. Returns true when A and B overlap.
    template <class SupportFn>
    static bool Intersect(const void* A, const void* B, SupportFn support, int maxSteps = 32) {
        float d[2] = {1.0f, 0.0f};
        Vec2 simplex[3];
        int count = 0;

        for (int iter = 0; iter < maxSteps; ++iter) {
            // Support point of Minkowski difference A - B along d.
            float a[2], b[2];
            support(A, d, a);
            float nd[2] = {-d[0], -d[1]};
            support(B, nd, b);
            float w[2] = {a[0]-b[0], a[1]-b[1]};

            if (dot(d, w) < 0) return false; // separated along d

            simplex[count][0] = w[0];
            simplex[count][1] = w[1];
            ++count;

            if (DoSimplex(simplex, count, d)) return true; // origin inside
            if (count == 0) return false;
            if (d[0] == 0 && d[1] == 0) return true; // origin reached
        }
        return true; // conservative: assumed overlap after max steps
    }

private:
    typedef float Vec2[2];
    static float dot(const float* a, const float* b) { return a[0]*b[0] + a[1]*b[1]; }
    static void sub(const float* a, const float* b, float* out) {
        out[0]=a[0]-b[0]; out[1]=a[1]-b[1];
    }
    static float cross(const float* a, const float* b) { return a[0]*b[1] - a[1]*b[0]; }
    static float len2(const float* a) { return a[0]*a[0] + a[1]*a[1]; }

    // Reduce the simplex and compute the next search direction d.
    // Returns true if the origin is inside the simplex (A and B overlap).
    static bool DoSimplex(Vec2* simplex, int& count, float* d) {
        if (count == 1) {
            d[0] = -simplex[0][0];
            d[1] = -simplex[0][1];
            return false;
        }
        if (count == 2) {
            return DoSegment(simplex, count, d);
        }
        // count == 3: triangle
        return DoTriangle(simplex, count, d);
    }

    // Reduce to the segment closest to the origin; update direction.
    static bool DoSegment(Vec2* simplex, int& count, float* d) {
        Vec2 a = {simplex[1][0], simplex[1][1]}; // most recent
        Vec2 b = {simplex[0][0], simplex[0][1]};
        Vec2 ab = {b[0]-a[0], b[1]-a[1]};

        // Perpendicular from origin to segment direction.
        // If origin is not above the segment, keep only b and search toward b.
        float t = -dot(a, ab) / (len2(ab) > 0 ? len2(ab) : 1);
        // Closest parameter on segment line (not clamped) of origin projection.
        // Compute closest point to origin on the segment.
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        float closest[2] = {a[0] + ab[0]*t, a[1] + ab[1]*t};

        // Shrink simplex to the vertices on the active side.
        if (t <= 0) {
            simplex[0][0]=b[0]; simplex[0][1]=b[1];
            count = 1;
            d[0] = -b[0]; d[1] = -b[1];
            return false;
        }
        if (t >= 1) {
            simplex[0][0]=a[0]; simplex[0][1]=a[1];
            count = 1;
            d[0] = -a[0]; d[1] = -a[1];
            return false;
        }
        // Origin projects to interior of segment -> origin on segment -> overlap.
        if (len2(closest) < 1e-8f) return true;
        // Keep both; search perpendicular to segment toward origin.
        d[0] = -closest[0]; d[1] = -closest[1];
        return false;
    }

    // Triangle case: check if origin is inside the triangle; otherwise reduce.
    static bool DoTriangle(Vec2* simplex, int& count, float* d) {
        Vec2 A = {simplex[2][0], simplex[2][1]};
        Vec2 B = {simplex[1][0], simplex[1][1]};
        Vec2 C = {simplex[0][0], simplex[0][1]};

        Vec2 ab = {B[0]-A[0], B[1]-A[1]};
        Vec2 ac = {C[0]-A[0], C[1]-A[1]};
        Vec2 ap = {-A[0], -A[1]};

        float abAp = cross(ab, ap);
        float acAp = cross(ac, ap);

        // Determine the region of the origin relative to edges AB and AC.
        float abac = cross(ab, ac);

        if (abac > 0) { // counter-clockwise triangle
            if (abAp > 0) {
                // Origin outside AB: reduce to segment A-B.
                simplex[0][0]=A[0]; simplex[0][1]=A[1];
                simplex[1][0]=B[0]; simplex[1][1]=B[1];
                count = 2;
                return DoSegment(simplex, count, d);
            }
            if (acAp < 0) {
                // Origin outside AC: reduce to segment A-C.
                simplex[0][0]=A[0]; simplex[0][1]=A[1];
                simplex[1][0]=C[0]; simplex[1][1]=C[1];
                count = 2;
                return DoSegment(simplex, count, d);
            }
        } else { // clockwise triangle
            if (abAp < 0) {
                simplex[0][0]=A[0]; simplex[0][1]=A[1];
                simplex[1][0]=B[0]; simplex[1][1]=B[1];
                count = 2;
                return DoSegment(simplex, count, d);
            }
            if (acAp > 0) {
                simplex[0][0]=A[0]; simplex[0][1]=A[1];
                simplex[1][0]=C[0]; simplex[1][1]=C[1];
                count = 2;
                return DoSegment(simplex, count, d);
            }
        }

        // Origin is between AB and AC areas: inside triangle unless behind BC.
        Vec2 bc = {C[0]-B[0], C[1]-B[1]};
        Vec2 bp = {-B[0], -B[1]};
        float bcBp = cross(bc, bp);
        if (bcBp > 0) {
            // Outside BC: reduce to segment B-C.
            simplex[0][0]=B[0]; simplex[0][1]=B[1];
            simplex[1][0]=C[0]; simplex[1][1]=C[1];
            count = 2;
            return DoSegment(simplex, count, d);
        }

        // Origin inside triangle -> overlap.
        return true;
    }
};

} // namespace bighero
