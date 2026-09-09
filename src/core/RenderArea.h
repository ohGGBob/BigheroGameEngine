#pragma once
#include <cstdint>

namespace bighero {

// RenderArea: describes the rectangular region of a render target affected by
// a render pass. Self-contained, std-lib only.
class RenderArea {
public:
    RenderArea() = default;
    RenderArea(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
        : x_(x), y_(y), width_(w), height_(h) {}

    void SetOffset(uint32_t x, uint32_t y) { x_=x; y_=y; }
    void SetExtent(uint32_t w, uint32_t h) { width_=w; height_=h; }
    uint32_t X() const { return x_; }
    uint32_t Y() const { return y_; }
    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }

    void Set(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { x_=x; y_=y; width_=w; height_=h; }
    bool IsEmpty() const { return width_==0 || height_==0; }
    uint64_t PixelCount() const { return (uint64_t)width_*height_; }
    uint32_t Right() const { return x_+width_; }
    uint32_t Bottom() const { return y_+height_; }
    bool Contains(uint32_t px, uint32_t py) const {
        return px>=x_ && px<Right() && py>=y_ && py<Bottom();
    }

private:
    uint32_t x_=0, y_=0, width_=0, height_=0;
};

} // namespace bighero
