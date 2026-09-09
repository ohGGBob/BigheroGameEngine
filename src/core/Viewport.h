#pragma once
#include <cstdint>

namespace bighero {

// Viewport: defines the scissor/viewport rectangle and depth range used during
// rasterization. Self-contained, std-lib only.
class Viewport {
public:
    Viewport() = default;
    Viewport(float x, float y, float w, float h, float minDepth = 0.0f, float maxDepth = 1.0f)
        : x_(x), y_(y), width_(w), height_(h), minDepth_(minDepth), maxDepth_(maxDepth) {}

    void SetX(float x) { x_ = x; }
    void SetY(float y) { y_ = y; }
    void SetWidth(float w) { width_ = w; }
    void SetHeight(float h) { height_ = h; }
    void SetMinDepth(float d) { minDepth_ = d; }
    void SetMaxDepth(float d) { maxDepth_ = d; }

    float X() const { return x_; }
    float Y() const { return y_; }
    float Width() const { return width_; }
    float Height() const { return height_; }
    float MinDepth() const { return minDepth_; }
    float MaxDepth() const { return maxDepth_; }

    void Set(float x, float y, float w, float h) { x_=x; y_=y; width_=w; height_=h; }
    bool IsEmpty() const { return width_ <= 0.0f || height_ <= 0.0f; }
    float CenterX() const { return x_ + width_ * 0.5f; }
    float CenterY() const { return y_ + height_ * 0.5f; }
    bool Contains(float px, float py) const {
        return px >= x_ && px <= x_+width_ && py >= y_ && py <= y_+height_;
    }
    float AspectRatio() const { return height_ > 0.0f ? width_/height_ : 0.0f; }

private:
    float x_=0, y_=0, width_=0, height_=0, minDepth_=0, maxDepth_=1;
};

} // namespace bighero
