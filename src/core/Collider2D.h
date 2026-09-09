#pragma once
#include <cmath>

namespace bighero {

// 2D collider shape descriptor: a circle or an AABB. Pure geometry data used
// by PhysicsWorld2D for contact tests. Lightweight and self-contained.
class Collider2D {
public:
    enum class Shape { Circle, Box };

    Collider2D() {}
    explicit Collider2D(float radius)
        : shape_(Shape::Circle), radius_(radius) {}
    Collider2D(float hw, float hh)
        : shape_(Shape::Box), halfW_(hw), halfH_(hh) {}

    void SetCircle(float r) { shape_ = Shape::Circle; radius_ = r; }
    void SetBox(float hw, float hh) { shape_ = Shape::Box; halfW_ = hw; halfH_ = hh; }
    Shape ShapeType() const { return shape_; }
    float Radius() const { return radius_; }
    void HalfExtents(float& hw, float& hh) const { hw = halfW_; hh = halfH_; }

    // Local-space transform relative to the body.
    void SetOffset(float ox, float oy) { ox_ = ox; oy_ = oy; }
    void Offset(float& ox, float& oy) const { ox = ox_; oy = oy_; }

    // Bounding circle radius (worst-case, for broadphase).
    float BoundingRadius() const {
        return shape_ == Shape::Circle ? radius_ : std::sqrt(halfW_ * halfW_ + halfH_ * halfH_);
    }

    static bool CircleCircle(float ax, float ay, float ar, float bx, float by, float br) {
        float dx = bx - ax, dy = by - ay;
        float rr = ar + br;
        return dx * dx + dy * dy <= rr * rr;
    }

    static bool PointInCircle(float px, float py, float cx, float cy, float r) {
        float dx = px - cx, dy = py - cy;
        return dx * dx + dy * dy <= r * r;
    }

private:
    Shape shape_ = Shape::Circle;
    float radius_ = 1.0f;
    float halfW_ = 0.5f, halfH_ = 0.5f;
    float ox_ = 0.0f, oy_ = 0.0f;
};

} // namespace bighero
