#pragma once
#include <cstdint>

namespace bighero {

// CapsuleCollider2D: a 2D capsule approximated by a rounded-rectangle collider
// (a segment with a radius). Self-contained, std-lib only.
class CapsuleCollider2D {
public:
    enum class Direction : int { Horizontal = 0, Vertical = 1 };

    CapsuleCollider2D() = default;
    CapsuleCollider2D(float radius, float length, Direction dir = Direction::Vertical)
        : radius_(radius), length_(length), dir_(dir) {}

    float Radius() const { return radius_; }
    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    // Total length along the capsule axis including the caps.
    float Length() const { return length_; }
    void SetLength(float l) { length_ = l < 0 ? 0 : l; }
    Direction CapsuleDirection() const { return dir_; }
    void SetDirection(Direction d) { dir_ = d; }

    float OffsetX() const { return offsetX_; }
    float OffsetY() const { return offsetY_; }
    void SetOffset(float x, float y) { offsetX_=x; offsetY_=y; }

    float Mass() const { return mass_; }
    void SetMass(float m) { mass_ = m < 0 ? 0 : m; }
    bool IsStatic() const { return mass_ == 0; }

    // The straight segment half-length (excluding caps).
    float SegmentHalfLength() const {
        float l = length_ - 2.0f * radius_;
        return l < 0 ? 0 : l * 0.5f;
    }

    // AABB half-extents given orientation.
    void AABBHalfExtents(float& hx, float& hy) const {
        float sh = SegmentHalfLength();
        if (dir_ == Direction::Horizontal) {
            hx = sh + radius_; hy = radius_;
        } else {
            hx = radius_; hy = sh + radius_;
        }
    }

private:
    float radius_ = 0.5f;
    float length_ = 1.0f;
    Direction dir_ = Direction::Vertical;
    float offsetX_ = 0, offsetY_ = 0;
    float mass_ = 1.0f;
};

} // namespace bighero
