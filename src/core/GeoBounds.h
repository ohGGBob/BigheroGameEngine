#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// GeoBounds: a geometry axis-aligned bounding box for 2D/3D points, with
// expansion, union, intersection and contains tests. Standard-library only,
// self-contained.
class GeoBounds {
public:
    GeoBounds() { Reset(); }

    // Reset to an empty (inverted) box.
    void Reset() {
        minX_=minY_=minZ_= 1e30f;
        maxX_=maxY_=maxZ_=-1e30f;
        valid_=false;
    }

    bool IsValid() const { return valid_; }

    void Expand(float x, float y) {
        if (x<minX_)minX_=x;
        if (x>maxX_)maxX_=x;
        if (y<minY_)minY_=y;
        if (y>maxY_)maxY_=y;
        valid_=true;
    }
    void Expand(float x, float y, float z) { Expand(x,y); if (z<minZ_)minZ_=z; if (z>maxZ_)maxZ_=z; }

    // Expand by a set of interleaved (x,y) points.
    void ExpandPoints(const std::vector<float>& pts) {
        for (std::size_t i=0; i+1<pts.size(); i+=2) Expand(pts[i], pts[i+1]);
    }

    void Set(float minX,float minY,float maxX,float maxY){
        minX_=minX;minY_=minY;maxX_=maxX;maxY_=maxY;valid_=true;
    }
    float MinX() const { return minX_; } float MinY() const { return minY_; }
    float MaxX() const { return maxX_; } float MaxY() const { return maxY_; }

    // Union of two bounds.
    void Union(const GeoBounds& o) {
        if (!o.valid_) return;
        if (!valid_) { *this = o; return; }
        if (o.minX_<minX_)minX_=o.minX_;
        if (o.maxX_>maxX_)maxX_=o.maxX_;
        if (o.minY_<minY_)minY_=o.minY_;
        if (o.maxY_>maxY_)maxY_=o.maxY_;
        if (o.minZ_<minZ_)minZ_=o.minZ_;
        if (o.maxZ_>maxZ_)maxZ_=o.maxZ_;
    }

    bool Contains(float x, float y) const {
        if (!valid_) return false;
        return x>=minX_ && x<=maxX_ && y>=minY_ && y<=maxY_;
    }

    // Intersects another AABB.
    bool Intersects(const GeoBounds& o) const {
        if (!valid_ || !o.valid_) return false;
        return !(o.minX_>maxX_ || o.maxX_<minX_ || o.minY_>maxY_ || o.maxY_<minY_);
    }

    float Width() const { return valid_ ? (maxX_-minX_) : 0; }
    float Height() const { return valid_ ? (maxY_-minY_) : 0; }

private:
    float minX_,minY_,minZ_,maxX_,maxY_,maxZ_;
    bool valid_=false;
};

} // namespace bighero
