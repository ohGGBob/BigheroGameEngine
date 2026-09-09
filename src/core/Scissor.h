#pragma once
#include <cstdint>

namespace bighero {

// Scissor: a rectangular scissor test region in framebuffer coordinates.
// Self-contained, std-lib only.
class Scissor {
public:
    Scissor() = default;
    Scissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
        : x_(x), y_(y), width_(width), height_(height) {}

    void SetOffset(int32_t x, int32_t y) { x_ = x; y_ = y; }
    void SetExtent(uint32_t w, uint32_t h) { width_ = w; height_ = h; }
    void Set(int32_t x, int32_t y, uint32_t w, uint32_t h) {
        x_ = x; y_ = y; width_ = w; height_ = h;
    }
    int32_t X() const { return x_; }
    int32_t Y() const { return y_; }
    int32_t Width() const { return (int32_t)width_; }
    int32_t Height() const { return (int32_t)height_; }
    uint32_t WidthU() const { return width_; }
    uint32_t HeightU() const { return height_; }

    int32_t Right() const { return x_ + (int32_t)width_; }
    int32_t Bottom() const { return y_ + (int32_t)height_; }
    uint64_t Area() const { return (uint64_t)width_ * height_; }
    bool IsEmpty() const { return width_ == 0 || height_ == 0; }
    bool Contains(int32_t px, int32_t py) const {
        return px >= x_ && px < x_ + (int32_t)width_ && py >= y_ && py < y_ + (int32_t)height_;
    }
    bool Overlaps(const Scissor& o) const {
        return !(o.x_ >= Right() || o.Right() <= x_ || o.y_ >= Bottom() || o.Bottom() <= y_);
    }
    bool IsFullScreen(uint32_t fbW, uint32_t fbH) const {
        return x_ == 0 && y_ == 0 && width_ == fbW && height_ == fbH;
    }

private:
    int32_t x_ = 0, y_ = 0;
    uint32_t width_ = 0, height_ = 0;
};

} // namespace bighero
