#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// LineSegment: a 2D line segment from A to B with length, midpoint, distance
// and point-at-t queries. Self-contained, std-lib only.
class LineSegment {
public:
    LineSegment() = default;
    LineSegment(float ax, float ay, float bx, float by)
        : ax_(ax), ay_(ay), bx_(bx), by_(by) {}

    void Set(float ax, float ay, float bx, float by) {
        ax_=ax; ay_=ay; bx_=bx; by_=by;
    }
    float Ax() const { return ax_; }
    float Ay() const { return ay_; }
    float Bx() const { return bx_; }
    float By() const { return by_; }

    float Dx() const { return bx_ - ax_; }
    float Dy() const { return by_ - ay_; }
    float Length() const { return std::sqrt(Dx()*Dx() + Dy()*Dy()); }
    float LengthSquared() const { return Dx()*Dx() + Dy()*Dy(); }

    float MidX() const { return (ax_ + bx_) * 0.5f; }
    float MidY() const { return (ay_ + by_) * 0.5f; }

    // Point at t in [0,1].
    void PointAt(float t, float& x, float& y) const {
        x = ax_ + Dx() * t;
        y = ay_ + Dy() * t;
    }

    // Distance from a point to this segment.
    float Distance(float px, float py) const {
        float dx = Dx(), dy = Dy();
        float len2 = dx*dx + dy*dy;
        if (len2 <= 0) { return std::sqrt((px-ax_)*(px-ax_) + (py-ay_)*(py-ay_)); }
        float t = ((px-ax_)*dx + (py-ay_)*dy) / len2;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float cx = ax_ + dx*t, cy = ay_ + dy*t;
        return std::sqrt((px-cx)*(px-cx) + (py-cy)*(py-cy));
    }

    // Closest parameter t on the segment to a point.
    float ClosestT(float px, float py) const {
        float dx = Dx(), dy = Dy();
        float len2 = dx*dx + dy*dy;
        if (len2 <= 0) return 0;
        float t = ((px-ax_)*dx + (py-ay_)*dy) / len2;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        return t;
    }

    // Direction as a unit vector; returns false if zero length.
    bool Direction(float& dx, float& dy) const {
        float len = Length();
        if (len <= 0) return false;
        dx = Dx()/len; dy = Dy()/len;
        return true;
    }

private:
    float ax_ = 0, ay_ = 0, bx_ = 0, by_ = 0;
};

} // namespace bighero
