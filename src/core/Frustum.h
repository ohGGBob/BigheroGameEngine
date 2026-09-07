#pragma once
#include <cmath>
#include <vector>
#include <array>

namespace bighero {

// View-frustum struct with point/sphere/AABB culling (simple axis-aligned frustum).
class Frustum {
public:
    enum PlaneIndex { Left, Right, Bottom, Top, Near, Far, Count };

    // Planes in Ax+By+Cz+D >= 0 form.
    void SetPlane(PlaneIndex i, float a, float b, float c, float d) {
        planes_[i][0] = a; planes_[i][1] = b; planes_[i][2] = c; planes_[i][3] = d;
    }

    bool PointInside(float x, float y, float z) const {
        for (int i = 0; i < Count; ++i)
            if (Distance(i, x, y, z) < 0) return false;
        return true;
    }

    bool SphereInside(float x, float y, float z, float radius) const {
        for (int i = 0; i < Count; ++i)
            if (Distance(i, x, y, z) < -radius) return false;
        return true;
    }

    bool AabbInside(float minX, float minY, float minZ,
                    float maxX, float maxY, float maxZ) const {
        for (int i = 0; i < Count; ++i) {
            // test negative vertex (most inside toward plane)
            float nx = (planes_[i][0] >= 0) ? minX : maxX;
            float ny = (planes_[i][1] >= 0) ? minY : maxY;
            float nz = (planes_[i][2] >= 0) ? minZ : maxZ;
            if (Distance(i, nx, ny, nz) < 0) return false;
        }
        return true;
    }

    void Normalize() {
        for (int i = 0; i < Count; ++i) {
            float len = std::sqrt(planes_[i][0]*planes_[i][0] +
                                  planes_[i][1]*planes_[i][1] +
                                  planes_[i][2]*planes_[i][2]);
            if (len > 0) for (int k = 0; k < 4; ++k) planes_[i][k] /= len;
        }
    }

private:
    float Distance(int i, float x, float y, float z) const {
        return planes_[i][0]*x + planes_[i][1]*y + planes_[i][2]*z + planes_[i][3];
    }
    std::array<std::array<float,4>, Count> planes_{};
};

} // namespace bighero
