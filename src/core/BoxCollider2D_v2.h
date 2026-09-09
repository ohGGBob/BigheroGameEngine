#pragma once
#include <cstdint>

namespace bighero {

// BoxCollider2D: an axis-aligned 2D box collider with half-extents and a
// center offset. Self-contained, std-lib only.
class BoxCollider2D {
public:
    BoxCollider2D() = default;
    BoxCollider2D(float halfW, float halfH) : halfW_(halfW), halfH_(halfH) {}

    float HalfWidth() const { return halfW_; }
    float HalfHeight() const { return halfH_; }
    void SetHalfExtents(float hw, float hh) {
        halfW_ = hw < 0 ? 0 : hw;
        halfH_ = hh < 0 ? 0 : hh;
    }
    float Width() const { return halfW_ * 2.0f; }
    float Height() const { return halfH_ * 2.0f; }

    float OffsetX() const { return offsetX_; }
    float OffsetY() const { return offsetY_; }
    void SetOffset(float x, float y) { offsetX_=x; offsetY_=y; }

    float Mass() const { return mass_; }
    void SetMass(float m) { mass_ = m < 0 ? 0 : m; }
    bool IsStatic() const { return mass_ == 0; }

    float MinX(float objX) const { return objX + offsetX_ - halfW_; }
    float MaxX(float objX) const { return objX + offsetX_ + halfW_; }
    float MinY(float objY) const { return objY + offsetY_ - halfH_; }
    float MaxY(float objY) const { return objY + offsetY_ + halfH_; }

    void SetRestitution(float r) { restitution_ = r < 0 ? 0 : (r > 1 ? 1 : r); }
    float Restitution() const { return restitution_; }

private:
    float halfW_ = 0.5f, halfH_ = 0.5f;
    float offsetX_ = 0, offsetY_ = 0;
    float mass_ = 1.0f;
    float restitution_ = 0.0f;
};

} // namespace bighero
