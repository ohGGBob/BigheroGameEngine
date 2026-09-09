#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// CircleFrustum: a 2D "frustum" approximated as a cone/circle with a view
// angle and distance, used for 2D visibility queries. Provides point/box
// inside and distance checks. Self-contained, std-lib only.
class CircleFrustum {
public:
    CircleFrustum() = default;
    CircleFrustum(float cx, float cy, float halfViewAngle, float distance)
        : cx_(cx), cy_(cy), halfView_(halfViewAngle), distance_(distance) {}

    void SetCenter(float x, float y) { cx_=x; cy_=y; }
    float CenterX() const { return cx_; }
    float CenterY() const { return cy_; }

    void SetHalfViewAngle(float rad) { halfView_ = rad; }
    float HalfViewAngle() const { return halfView_; }
    void SetDistance(float d) { distance_ = d; }
    float Distance() const { return distance_; }

    // Whether a point is within the frustum circle (distance <= distance_).
    bool Contains(float px, float py) const {
        float dx = px-cx_, dy = py-cy_;
        float d = std::sqrt(dx*dx + dy*dy);
        return d <= distance_;
    }

    // Whether a point is within both the distance and the view angle relative
    // to a facing direction (fx, fy).
    bool Contains(float px, float py, float fx, float fy) const {
        float vx = px-cx_, vy = py-cy_;
        float d = std::sqrt(vx*vx + vy*vy);
        if (d > distance_) return false;
        if (d <= 0) return true;
        float dot = (vx*fx + vy*fy) / d; // cos angle to facing
        return dot >= std::cos(halfView_);
    }

    float Area() const { return 3.14159265f * distance_ * distance_; }

private:
    float cx_=0, cy_=0, halfView_=0, distance_=10.0f;
};

} // namespace bighero
