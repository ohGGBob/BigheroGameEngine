#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// Ray2D: a 2D ray defined by an origin and a direction. Self-contained,
// std-lib only.
class Ray2D {
public:
    Ray2D() = default;
    Ray2D(float ox, float oy, float dx, float dy) : ox_(ox), oy_(oy) { SetDirection(dx, dy); }

    void SetOrigin(float ox, float oy) { ox_=ox; oy_=oy; }
    float OriginX() const { return ox_; }
    float OriginY() const { return oy_; }

    void SetDirection(float dx, float dy) {
        float len = std::sqrt(dx*dx + dy*dy);
        if (len > 0) { dx_ = dx/len; dy_ = dy/len; }
        else { dx_ = 0; dy_ = 0; }
    }
    float DirectionX() const { return dx_; }
    float DirectionY() const { return dy_; }
    bool IsValid() const { return dx_ != 0 || dy_ != 0; }

    // Point at distance t along the ray.
    void PointAt(float t, float& x, float& y) const {
        x = ox_ + dx_ * t;
        y = oy_ + dy_ * t;
    }

    float Dot(const Ray2D& other) const { return dx_*other.dx_ + dy_*other.dy_; }

    // Distance from a point to the ray (infinite line clamped to t>=0).
    float Distance(float px, float py) const {
        if (!IsValid()) return std::sqrt((px-ox_)*(px-ox_) + (py-oy_)*(py-oy_));
        float t = (px-ox_)*dx_ + (py-oy_)*dy_;
        if (t < 0) t = 0;
        float cx = ox_ + dx_*t, cy = oy_ + dy_*t;
        return std::sqrt((px-cx)*(px-cx) + (py-cy)*(py-cy));
    }

    // Closest parameter t to a point (clamped to t>=0).
    float ClosestT(float px, float py) const {
        float t = (px-ox_)*dx_ + (py-oy_)*dy_;
        return t < 0 ? 0 : t;
    }

private:
    float ox_ = 0, oy_ = 0, dx_ = 1, dy_ = 0;
};

} // namespace bighero
