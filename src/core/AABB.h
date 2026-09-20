#pragma once
#include <algorithm>
#include <cmath>

namespace bighero
{

// AABB: an axis-aligned bounding box in 2D with min/max corners, containment,
// overlap, and union/expand helpers. Self-contained.
struct AABB
{
    float minX = 0, minY = 0;
    float maxX = 0, maxY = 0;

    AABB() = default;
    AABB(float minX_, float minY_, float maxX_, float maxY_) : minX(minX_), minY(minY_), maxX(maxX_), maxY(maxY_) {}

    float Width() const { return maxX - minX; }
    float Height() const { return maxY - minY; }
    float CenterX() const { return (minX + maxX) * 0.5f; }
    float CenterY() const { return (minY + maxY) * 0.5f; }

    bool Contains(float px, float py) const { return px >= minX && px <= maxX && py >= minY && py <= maxY; }
    bool Overlaps(const AABB& o) const { return minX <= o.maxX && o.minX <= maxX && minY <= o.maxY && o.minY <= maxY; }
    void Expand(float amt)
    {
        minX -= amt;
        minY -= amt;
        maxX += amt;
        maxY += amt;
    }
    void Encapsulate(float px, float py)
    {
        minX = std::min(minX, px);
        minY = std::min(minY, py);
        maxX = std::max(maxX, px);
        maxY = std::max(maxY, py);
    }
    static AABB FromCenter(float cx, float cy, float hw, float hh) { return AABB(cx - hw, cy - hh, cx + hw, cy + hh); }
};

} // namespace bighero
