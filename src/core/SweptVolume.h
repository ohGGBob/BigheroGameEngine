#pragma once
#include <cstddef>
#include <vector>

namespace bighero {

// SweptVolume: a swept volume descriptor used for continuous collision
// detection — a shape moving from a start to an end pose. Pure CPU-side
// data container, self-contained.
class SweptVolume {
public:
    enum class ShapeType { Sphere, Box, Capsule };

    SweptVolume() {}
    SweptVolume(ShapeType type) : type_(type) {}

    void SetType(ShapeType t) { type_ = t; }
    ShapeType Type() const { return type_; }

    void SetStart(float x, float y, float z) { sx_=x; sy_=y; sz_=z; }
    void SetEnd(float x, float y, float z) { ex_=x; ey_=y; ez_=z; }
    void End(float& x, float& y, float& z) const { x=ex_; y=ey_; z=ez_; }

    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }
    void SetHalfHeight(float h) { halfH_ = h < 0 ? 0 : h; }
    float HalfHeight() const { return halfH_; }

    // Interpolated center at fraction t in [0,1].
    void CenterAt(float t, float& x, float& y, float& z) const {
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        x = sx_ + (ex_-sx_)*t; y = sy_ + (ey_-sy_)*t; z = sz_ + (ez_-sz_)*t;
    }

    // Conservative bounding extent along the sweep (radius + half travel).
    float SweepLength() const {
        float dx = ex_-sx_, dy = ey_-sy_, dz = ez_-sz_;
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    }

private:
    ShapeType type_ = ShapeType::Sphere;
    float sx_=0, sy_=0, sz_=0;
    float ex_=0, ey_=0, ez_=0;
    float radius_=1.0f;
    float halfH_=1.0f;
};

} // namespace bighero
