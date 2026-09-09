#pragma once
#include <cstdint>

namespace bighero {

// ScissorState: a scissor rectangle descriptor (in pixels) used to mask
// rasterization, with a point-in-scissor test. Self-contained.
struct ScissorState {
    int x = 0, y = 0;   // top-left in pixels
    int width = 0, height = 0;  // 0 in either dimension disables the test
    bool enabled = false;

    ScissorState() = default;
    ScissorState(int x_, int y_, int w_, int h_, bool enabled_ = true)
        : x(x_), y(y_), width(w_), height(h_), enabled(enabled_) {}

    void Set(int x_, int y_, int w_, int h_) {
        x = x_; y = y_; width = w_; height = h_;
        enabled = w_ > 0 && h_ > 0;
    }
    void Disable() { enabled = false; }
    // Test whether a pixel at (px,py) passes the scissor, if enabled.
    bool Test(int px, int py) const {
        if (!enabled || width <= 0 || height <= 0) return true;
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    void Expand(int amt) {
        x -= amt; y -= amt; width += amt * 2; height += amt * 2;
        if (width < 0) width = 0;
        if (height < 0) height = 0;
    }
};

} // namespace bighero
