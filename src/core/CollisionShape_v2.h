#pragma once
#include <cstdint>

namespace bighero {

// CollisionShape: a tagged union-like descriptor for a convex collision shape
// (sphere, box, capsule, cylinder) with scale parameters and a local center.
// Self-contained, std-lib only.
class CollisionShape {
public:
    enum class Type : int {
        Sphere = 0, Box = 1, Capsule = 2, Cylinder = 3, None = 4
    };

    CollisionShape() = default;
    void SetType(Type t) { type_ = t; }
    Type GetType() const { return type_; }

    void SetCenter(float x, float y, float z) { cx_=x; cy_=y; cz_=z; }
    float CenterX() const { return cx_; }
    float CenterY() const { return cy_; }
    float CenterZ() const { return cz_; }

    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }

    void SetHalfExtents(float hx, float hy, float hz) {
        hx_=hx<0?0:hx; hy_=hy<0?0:hy; hz_=hz<0?0:hz;
    }
    float HalfExtentsX() const { return hx_; }
    float HalfExtentsY() const { return hy_; }
    float HalfExtentsZ() const { return hz_; }

    void SetHeight(float h) { height_ = h < 0 ? 0 : h; }
    float Height() const { return height_; }

    bool IsValid() const { return type_ != Type::None; }
    const char* TypeName() const {
        switch (type_) {
            case Type::Sphere: return "Sphere";
            case Type::Box: return "Box";
            case Type::Capsule: return "Capsule";
            case Type::Cylinder: return "Cylinder";
            case Type::None: return "None";
        }
        return "Unknown";
    }

private:
    Type type_ = Type::None;
    float cx_=0, cy_=0, cz_=0;
    float radius_ = 0.5f;
    float hx_=0, hy_=0, hz_=0;
    float height_ = 1.0f;
};

} // namespace bighero
