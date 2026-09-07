#pragma once
#include <algorithm>
#include <cmath>

namespace bighero {

// Axis-aligned bounding box in 3D with union/intersect/contains helpers.
struct BoundingBox {
    float minX, minY, minZ, maxX, maxY, maxZ;

    BoundingBox() : minX(0), minY(0), minZ(0), maxX(0), maxY(0), maxZ(0) {}
    BoundingBox(float mnx, float mny, float mnz, float mxx, float mxy, float mxz)
        : minX(mnx), minY(mny), minZ(mnz), maxX(mxx), maxY(mxy), maxZ(mxz) {}

    static BoundingBox FromCenterExtents(float cx, float cy, float cz,
                                         float ex, float ey, float ez) {
        return BoundingBox(cx - ex, cy - ey, cz - ez, cx + ex, cy + ey, cz + ez);
    }

    void Set(float mnx, float mny, float mnz, float mxx, float mxy, float mxz) {
        minX = mnx; minY = mny; minZ = mnz; maxX = mxx; maxY = mxy; maxZ = mxz;
    }

    float Width()  const { return maxX - minX; }
    float Height() const { return maxY - minY; }
    float Depth()  const { return maxZ - minZ; }
    float Volume() const { return Width() * Height() * Depth(); }
    float CenterX() const { return (minX + maxX) * 0.5f; }
    float CenterY() const { return (minY + maxY) * 0.5f; }
    float CenterZ() const { return (minZ + maxZ) * 0.5f; }

    bool Contains(float x, float y, float z) const {
        return x >= minX && x <= maxX && y >= minY && y <= maxY && z >= minZ && z <= maxZ;
    }
    bool Contains(const BoundingBox& o) const {
        return o.minX >= minX && o.maxX <= maxX &&
               o.minY >= minY && o.maxY <= maxY &&
               o.minZ >= minZ && o.maxZ <= maxZ;
    }

    bool Intersects(const BoundingBox& o) const {
        return minX <= o.maxX && maxX >= o.minX &&
               minY <= o.maxY && maxY >= o.minY &&
               minZ <= o.maxZ && maxZ >= o.minZ;
    }

    void ExpandBy(float x, float y, float z) {
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
        minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
    }
    void Expand(float amount) {
        minX -= amount; maxX += amount;
        minY -= amount; maxY += amount;
        minZ -= amount; maxZ += amount;
    }

    static BoundingBox Union(const BoundingBox& a, const BoundingBox& b) {
        return BoundingBox(std::min(a.minX, b.minX), std::min(a.minY, b.minY), std::min(a.minZ, b.minZ),
                           std::max(a.maxX, b.maxX), std::max(a.maxY, b.maxY), std::max(a.maxZ, b.maxZ));
    }

    // Returns intersection box, or an empty (negative) box if disjoint.
    static BoundingBox Intersection(const BoundingBox& a, const BoundingBox& b) {
        BoundingBox r(std::max(a.minX, b.minX), std::max(a.minY, b.minY), std::max(a.minZ, b.minZ),
                      std::min(a.maxX, b.maxX), std::min(a.maxY, b.maxY), std::min(a.maxZ, b.maxZ));
        if (r.minX > r.maxX || r.minY > r.maxY || r.minZ > r.maxZ) {
            r.Set(0,0,0,0,0,0); // degenerate/empty sentinel
        }
        return r;
    }

    bool operator==(const BoundingBox& o) const {
        return minX==o.minX && minY==o.minY && minZ==o.minZ &&
               maxX==o.maxX && maxY==o.maxY && maxZ==o.maxZ;
    }
};

} // namespace bighero
