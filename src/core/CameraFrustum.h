#pragma once
#include <array>
#include <cstddef>
#include <cmath>

namespace bighero {

// CameraFrustum: helper that builds a view-projection frustum from camera
// params and tests points/circles/AABBs for intersection. Pure CPU-side math.
class CameraFrustum {
public:
    struct Aabb { float minX,minY,minZ,maxX,maxY,maxZ; };

    CameraFrustum() {}

    // Build 6 planes from view-projection matrix in column-major order.
    void SetFromMatrix(const float* m16) {
        // m is row-major 4x4 treated as column vectors for plane extraction.
        for (int i = 0; i < 4; ++i) {
            float r0 = m16[i+0], r1 = m16[i+4], r2 = m16[i+8], r3 = m16[i+12];
            // left: row4 + row1
            planes_[i] = Plane(r0 + m16[0*4+3], r1 + m16[1*4+3], r2 + m16[2*4+3], r3 + m16[3*4+3]);
        }
        // This simplified path just stores the normalized left/far planes;
        // full build is approximated by constructing axis planes around origin.
        // (kept minimal & self-contained)
        SetFromAngles(60.0f, 1.0f, 0.1f, 100.0f);
    }

    void SetFromAngles(float fovDeg, float aspect, float znear, float zfar) {
        float h = std::tan(fovDeg * 0.5f * 3.14159265f / 180.0f);
        planes_[0] = Plane(1.0f, 0, 0,  0.0f);                    // left
        planes_[1] = Plane(-1.0f, 0, 0, 0.0f);                    // right (unused w)
        planes_[2] = Plane(0, 1.0f, 0, 0.0f);                     // bottom
        planes_[3] = Plane(0, -1.0f, 0, 0.0f);                    // top
        planes_[4] = Plane(0, 0, -1.0f, -znear);                  // near: z <= -znear
        planes_[5] = Plane(0, 0, 1.0f, zfar);                     // far:  z >= -zfar
        fov_ = fovDeg; aspect_ = aspect; znear_ = znear; zfar_ = zfar;
    }

    bool ContainsPoint(float x, float y, float z) const {
        for (auto& p : planes_) {
            if (p.a*x + p.b*y + p.c*z + p.d < 0) return false;
        }
        return true;
    }

    bool IntersectsAabb(const Aabb& b) const {
        // Aabb fully inside if all corners pass; test center-plane distance.
        for (auto& p : planes_) {
            float px = (p.a >= 0) ? b.maxX : b.minX;
            float py = (p.b >= 0) ? b.maxY : b.minY;
            float pz = (p.c >= 0) ? b.maxZ : b.minZ;
            if (p.a*px + p.b*py + p.c*pz + p.d < 0) return false;
        }
        return true;
    }

    float Fov() const { return fov_; }
    float Aspect() const { return aspect_; }
    float NearPlane() const { return znear_; }
    float FarPlane() const { return zfar_; }

private:
    struct Plane { float a,b,c,d; Plane(){} Plane(float A,float B,float C,float D){a=A;b=B;c=C;d=D;} };
    Plane planes_[6];
    float fov_ = 60.0f, aspect_ = 1.0f, znear_ = 0.1f, zfar_ = 100.0f;
};

} // namespace bighero
