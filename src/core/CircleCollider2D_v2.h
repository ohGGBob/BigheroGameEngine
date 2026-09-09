#pragma once
#include <cstdint>

namespace bighero {

// CircleCollider2D: a basic 2D circle collider with radius, center offset and
// mass. Used by 2D physics/AABB broadphase tests. Self-contained, std-lib only.
class CircleCollider2D {
public:
    CircleCollider2D() = default;
    CircleCollider2D(float radius) : radius_(radius) {}
    CircleCollider2D(float radius, float offsetX, float offsetY)
        : radius_(radius), offsetX_(offsetX), offsetY_(offsetY) {}

    float Radius() const { return radius_; }
    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }

    float OffsetX() const { return offsetX_; }
    float OffsetY() const { return offsetY_; }
    void SetOffset(float x, float y) { offsetX_=x; offsetY_=y; }

    float Mass() const { return mass_; }
    void SetMass(float m) { mass_ = m < 0 ? 0 : m; }
    bool IsStatic() const { return mass_ == 0; }

    // Center in world space given the object position.
    float CenterX(float objX) const { return objX + offsetX_; }
    float CenterY(float objY) const { return objY + offsetY_; }

    // Swept AABB half-extent along x/y given radius and rotation is irrelevant
    // for a circle.
    float ExtentX() const { return radius_; }
    float ExtentY() const { return radius_; }

    void SetRestitution(float r) { restitution_ = r < 0 ? 0 : (r > 1 ? 1 : r); }
    float Restitution() const { return restitution_; }

private:
    float radius_ = 1.0f;
    float offsetX_ = 0, offsetY_ = 0;
    float mass_ = 1.0f;
    float restitution_ = 0.0f;
};

} // namespace bighero
