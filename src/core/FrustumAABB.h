#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// FrustumAABB: an axis-aligned bounding box used to represent a frustum's
// spatial extent, with Plane/AABB overlap helpers. Provides a way to check
// whether an AABB is inside, intersecting, or outside a box volume.
// Self-contained, std-lib only.
class FrustumAABB {
public:
    FrustumAABB() = default;
    FrustumAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
        : minX_(minX), minY_(minY), minZ_(minZ), maxX_(maxX), maxY_(maxY), maxZ_(maxZ) {}

    void Set(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
        minX_=minX; minY_=minY; minZ_=minZ; maxX_=maxX; maxY_=maxY; maxZ_=maxZ;
    }
    float MinX() const { return minX_; }
    float MinY() const { return minY_; }
    float MinZ() const { return minZ_; }
    float MaxX() const { return maxX_; }
    float MaxY() const { return maxY_; }
    float MaxZ() const { return maxZ_; }

    // Whether a point falls within the box.
    bool Contains(float x, float y, float z) const {
        return x>=minX_ && x<=maxX_ && y>=minY_ && y<=maxY_ && z>=minZ_ && z<=maxZ_;
    }
    // Whether another AABB overlaps this one.
    bool Overlaps(const FrustumAABB& o) const {
        return !(o.minX_>maxX_ || o.maxX_<minX_ || o.minY_>maxY_ || o.maxY_<minY_ || o.minZ_>maxZ_ || o.maxZ_<minZ_);
    }
    // Whether another AABB is fully inside this one.
    bool ContainsBox(const FrustumAABB& o) const {
        return o.minX_>=minX_ && o.maxX_<=maxX_ && o.minY_>=minY_ && o.maxY_<=maxY_ && o.minZ_>=minZ_ && o.maxZ_<=maxZ_;
    }
    float Volume() const {
        return (maxX_-minX_)*(maxY_-minY_)*(maxZ_-minZ_);
    }
    void Encapsulate(const FrustumAABB& o) {
        if (o.minX_<minX_) minX_=o.minX_;
        if (o.minY_<minY_) minY_=o.minY_;
        if (o.minZ_<minZ_) minZ_=o.minZ_;
        if (o.maxX_>maxX_) maxX_=o.maxX_;
        if (o.maxY_>maxY_) maxY_=o.maxY_;
        if (o.maxZ_>maxZ_) maxZ_=o.maxZ_;
    }

private:
    float minX_=0,minY_=0,minZ_=0,maxX_=0,maxY_=0,maxZ_=0;
};

} // namespace bighero
