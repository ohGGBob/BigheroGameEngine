#pragma once
#include <cmath>
#include <cstdint>

namespace bighero
{

// Sphere3: a 3D sphere defined by center and radius. Provides volume, surface
// area and containment queries. Self-contained, std-lib only.
class Sphere3
{
  public:
    Sphere3() = default;
    Sphere3(float cx, float cy, float cz, float radius) : cx_(cx), cy_(cy), cz_(cz), radius_(radius) {}

    float CenterX() const { return cx_; }
    float CenterY() const { return cy_; }
    float CenterZ() const { return cz_; }
    void SetCenter(float x, float y, float z)
    {
        cx_ = x;
        cy_ = y;
        cz_ = z;
    }

    float Radius() const { return radius_; }
    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }

    float Volume() const { return (4.0f / 3.0f) * 3.14159265f * radius_ * radius_ * radius_; }
    float SurfaceArea() const { return 4.0f * 3.14159265f * radius_ * radius_; }
    float Diameter() const { return radius_ * 2.0f; }

    bool Contains(float x, float y, float z) const
    {
        float dx = x - cx_, dy = y - cy_, dz = z - cz_;
        return dx * dx + dy * dy + dz * dz <= radius_ * radius_;
    }
    bool Contains(const Sphere3& o) const
    {
        float dx = o.cx_ - cx_, dy = o.cy_ - cy_, dz = o.cz_ - cz_;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        return d + o.radius_ <= radius_;
    }
    bool Intersects(const Sphere3& o) const
    {
        float dx = o.cx_ - cx_, dy = o.cy_ - cy_, dz = o.cz_ - cz_;
        float d2 = dx * dx + dy * dy + dz * dz;
        float r = radius_ + o.radius_;
        return d2 <= r * r;
    }
    float DistanceTo(const Sphere3& o) const
    {
        float dx = o.cx_ - cx_, dy = o.cy_ - cy_, dz = o.cz_ - cz_;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

  private:
    float cx_ = 0, cy_ = 0, cz_ = 0, radius_ = 1.0f;
};

} // namespace bighero
