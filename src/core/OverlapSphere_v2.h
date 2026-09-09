#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// OverlapSphere: a sphere overlap query result plus the math to test whether a
// sphere overlaps a box or another sphere. Self-contained, std-lib only.
class OverlapSphere {
public:
    OverlapSphere() = default;
    OverlapSphere(float cx, float cy, float cz, float radius)
        : cx_(cx), cy_(cy), cz_(cz), radius_(radius) {}

    float CenterX() const { return cx_; }
    float CenterY() const { return cy_; }
    float CenterZ() const { return cz_; }
    float Radius() const { return radius_; }
    void Set(float cx, float cy, float cz, float radius) {
        cx_=cx; cy_=cy; cz_=cz; radius_=radius<0?0:radius;
    }

    // Overlap with another sphere.
    bool Overlaps(const OverlapSphere& o) const {
        float dx=o.cx_-cx_, dy=o.cy_-cy_, dz=o.cz_-cz_;
        float r = radius_ + o.radius_;
        return dx*dx + dy*dy + dz*dz <= r*r;
    }

    // Overlap with an axis-aligned box {minX,minY,minZ,maxX,maxY,maxZ}.
    bool OverlapsBox(const float* box) const {
        float nx = cx_ < box[0] ? box[0] : (cx_ > box[3] ? box[3] : cx_);
        float ny = cy_ < box[1] ? box[1] : (cy_ > box[4] ? box[4] : cy_);
        float nz = cz_ < box[2] ? box[2] : (cz_ > box[5] ? box[5] : cz_);
        float dx = cx_-nx, dy = cy_-ny, dz = cz_-nz;
        return dx*dx + dy*dy + dz*dz <= radius_*radius_;
    }

    // Does this sphere overlap a point?
    bool ContainsPoint(float px, float py, float pz) const {
        float dx=px-cx_, dy=py-cy_, dz=pz-cz_;
        return dx*dx + dy*dy + dz*dz <= radius_*radius_;
    }

    float Volume() const { return (4.0f/3.0f)*3.14159265f*radius_*radius_*radius_; }

private:
    float cx_=0, cy_=0, cz_=0, radius_=1.0f;
};

} // namespace bighero
