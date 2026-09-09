#pragma once
#include <cstdint>

namespace bighero {

// Rect2D: a 2D axis-aligned rectangle defined by min/max corners. Provides
// intersection, containment, union and size/center helpers.
// Self-contained, std-lib only.
class Rect2D {
public:
    Rect2D() = default;
    Rect2D(float minX, float minY, float maxX, float maxY)
        : minX_(minX), minY_(minY), maxX_(maxX), maxY_(maxY) {}

    float MinX() const { return minX_; }
    float MinY() const { return minY_; }
    float MaxX() const { return maxX_; }
    float MaxY() const { return maxY_; }
    void Set(float minX, float minY, float maxX, float maxY) {
        minX_=minX; minY_=minY; maxX_=maxX; maxY_=maxY;
    }

    float Width() const { return maxX_ - minX_; }
    float Height() const { return maxY_ - minY_; }
    float CenterX() const { return (minX_ + maxX_) * 0.5f; }
    float CenterY() const { return (minY_ + maxY_) * 0.5f; }
    bool IsEmpty() const { return maxX_ <= minX_ || maxY_ <= minY_; }
    float Area() const { return Width() * Height(); }

    bool Contains(float x, float y) const {
        return x >= minX_ && x <= maxX_ && y >= minY_ && y <= maxY_;
    }
    bool Contains(const Rect2D& o) const {
        return o.minX_ >= minX_ && o.maxX_ <= maxX_ &&
               o.minY_ >= minY_ && o.maxY_ <= maxY_;
    }
    bool Intersects(const Rect2D& o) const {
        return !(o.minX_ > maxX_ || o.maxX_ < minX_ ||
                 o.minY_ > maxY_ || o.maxY_ < minY_);
    }

    // Return the intersection rect (empty if disjoint).
    Rect2D Intersect(const Rect2D& o) const {
        float nx = minX_ > o.minX_ ? minX_ : o.minX_;
        float ny = minY_ > o.minY_ ? minY_ : o.minY_;
        float fx = maxX_ < o.maxX_ ? maxX_ : o.maxX_;
        float fy = maxY_ < o.maxY_ ? maxY_ : o.maxY_;
        return Rect2D(nx, ny, fx, fy);
    }

    void Encapsulate(float x, float y) {
        if (x < minX_) minX_ = x;
        if (y < minY_) minY_ = y;
        if (x > maxX_) maxX_ = x;
        if (y > maxY_) maxY_ = y;
    }
    void Encapsulate(const Rect2D& o) {
        Encapsulate(o.minX_, o.minY_);
        Encapsulate(o.maxX_, o.maxY_);
    }

    void Inflate(float d) {
        minX_ -= d; minY_ -= d; maxX_ += d; maxY_ += d;
    }
    void Normalize() {
        if (minX_ > maxX_) { float t = minX_; minX_ = maxX_; maxX_ = t; }
        if (minY_ > maxY_) { float t = minY_; minY_ = maxY_; maxY_ = t; }
    }

private:
    float minX_ = 0, minY_ = 0, maxX_ = 0, maxY_ = 0;
};

} // namespace bighero
