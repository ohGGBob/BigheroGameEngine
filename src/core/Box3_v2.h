#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// Box3: an axis-aligned 3D box defined by min/max corners. Provides
// containment, intersection, union, size and center helpers.
// Self-contained, std-lib only.
class Box3 {
public:
    Box3() = default;
    Box3(float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
        : minX_(minX), minY_(minY), minZ_(minZ), maxX_(maxX), maxY_(maxY), maxZ_(maxZ) {}

    float MinX() const { return minX_; }
    float MinY() const { return minY_; }
    float MinZ() const { return minZ_; }
    float MaxX() const { return maxX_; }
    float MaxY() const { return maxY_; }
    float MaxZ() const { return maxZ_; }

    float Width() const { return maxX_ - minX_; }
    float Height() const { return maxY_ - minY_; }
    float Depth() const { return maxZ_ - minZ_; }
    float Volume() const { return Width() * Height() * Depth(); }
    float CenterX() const { return (minX_+maxX_)*0.5f; }
    float CenterY() const { return (minY_+maxY_)*0.5f; }
    float CenterZ() const { return (minZ_+maxZ_)*0.5f; }
    bool IsEmpty() const { return maxX_<=minX_ || maxY_<=minY_ || maxZ_<=minZ_; }

    bool Contains(float x, float y, float z) const {
        return x>=minX_ && x<=maxX_ && y>=minY_ && y<=maxY_ && z>=minZ_ && z<=maxZ_;
    }
    bool Contains(const Box3& o) const {
        return o.minX_>=minX_ && o.maxX_<=maxX_ &&
               o.minY_>=minY_ && o.maxY_<=maxY_ &&
               o.minZ_>=minZ_ && o.maxZ_<=maxZ_;
    }
    bool Intersects(const Box3& o) const {
        return !(o.minX_>maxX_ || o.maxX_<minX_ ||
                 o.minY_>maxY_ || o.maxY_<minY_ ||
                 o.minZ_>maxZ_ || o.maxZ_<minZ_);
    }
    Box3 Intersect(const Box3& o) const {
        return Box3(minX_>o.minX_?minX_:o.minX_, minY_>o.minY_?minY_:o.minY_, minZ_>o.minZ_?minZ_:o.minZ_,
                    maxX_<o.maxX_?maxX_:o.maxX_, maxY_<o.maxY_?maxY_:o.maxY_, maxZ_<o.maxZ_?maxZ_:o.maxZ_);
    }
    void Encapsulate(float x, float y, float z) {
        if (x<minX_) minX_=x;
        if (y<minY_) minY_=y;
        if (z<minZ_) minZ_=z;
        if (x>maxX_) maxX_=x;
        if (y>maxY_) maxY_=y;
        if (z>maxZ_) maxZ_=z;
    }
    void Inflate(float d) {
        minX_-=d; minY_-=d; minZ_-=d; maxX_+=d; maxY_+=d; maxZ_+=d;
    }
    void Normalize() {
        if (minX_>maxX_){float t=minX_;minX_=maxX_;maxX_=t;}
        if (minY_>maxY_){float t=minY_;minY_=maxY_;maxY_=t;}
        if (minZ_>maxZ_){float t=minZ_;minZ_=maxZ_;maxZ_=t;}
    }
private:
    float minX_=0,minY_=0,minZ_=0,maxX_=0,maxY_=0,maxZ_=0;
};

} // namespace bighero
