#pragma once
#include <cstdint>
#include <vector>
#include <array>

namespace bighero {

// HermiteCurve: a cubic Hermite spline segment defined by two endpoints and two
// tangents. Self-contained, std-lib only.
class HermiteCurve {
public:
    HermiteCurve() = default;
    // Endpoints P0/P1 and tangents T0/T1 expressed as 3D vectors.
    HermiteCurve(float p0x, float p0y, float p0z,
                 float p1x, float p1y, float p1z,
                 float t0x, float t0y, float t0z,
                 float t1x, float t1y, float t1z) {
        Set(p0x,p0y,p0z, p1x,p1y,p1z, t0x,t0y,t0z, t1x,t1y,t1z);
    }

    void Set(float p0x,float p0y,float p0z, float p1x,float p1y,float p1z,
             float t0x,float t0y,float t0z, float t1x,float t1y,float t1z) {
        p0_ = {p0x,p0y,p0z}; p1_ = {p1x,p1y,p1z};
        t0_ = {t0x,t0y,t0z}; t1_ = {t1x,t1y,t1z};
    }

    // Evaluate at t in [0,1]. Cubic Hermite basis:
    //   h00 = 2t^3 - 3t^2 + 1, h10 = t^3 - 2t^2 + t
    //   h01 = -2t^3 + 3t^2,    h11 = t^3 - t^2
    void Evaluate(float t, float* out) const {
        float t2 = t*t, t3 = t2*t;
        float h00 = 2*t3 - 3*t2 + 1;
        float h10 = t3 - 2*t2 + t;
        float h01 = -2*t3 + 3*t2;
        float h11 = t3 - t2;
        for (int c = 0; c < 3; ++c) {
            out[c] = h00*p0_[c] + h10*t0_[c] + h01*p1_[c] + h11*t1_[c];
        }
    }

    void Endpoint0(float* p) const { p[0]=p0_[0]; p[1]=p0_[1]; p[2]=p0_[2]; }
    void Endpoint1(float* p) const { p[0]=p1_[0]; p[1]=p1_[1]; p[2]=p1_[2]; }

    // Extract a Hermite segment between two consecutive points of a polyline,
    // using the average of adjacent segment directions as tangents.
    static void FromTwoPoints(const float* a, const float* b, float* defaultTangent,
                              HermiteCurve& out) {
        // Tangents default to (b-a) unless provided.
        float tx = defaultTangent ? defaultTangent[0] : (b[0]-a[0]);
        float ty = defaultTangent ? defaultTangent[1] : (b[1]-a[1]);
        float tz = defaultTangent ? defaultTangent[2] : (b[2]-a[2]);
        out.Set(a[0],a[1],a[2], b[0],b[1],b[2], tx,ty,tz, tx,ty,tz);
    }

private:
    std::array<float,3> p0_={0,0,0}, p1_={0,0,0}, t0_={0,0,0}, t1_={0,0,0};
};

} // namespace bighero
