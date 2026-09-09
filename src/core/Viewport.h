#pragma once
#include <algorithm>

namespace bighero {

// 2D viewport rectangle and helpers for mapping normalized coords.
struct Viewport {
    int x, y, width, height;

    Viewport() : x(0), y(0), width(0), height(0) {}
    Viewport(int x_, int y_, int w_, int h_) : x(x_), y(y_), width(w_), height(h_) {}

    bool Empty() const { return width <= 0 || height <= 0; }

    int Right() const { return x + width; }
    int Bottom() const { return y + height; }

    bool Contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }

    void ClampTo(int px, int py, int& ox, int& oy) const {
        ox = std::max(x, std::min(px, x + width - 1));
        oy = std::max(y, std::min(py, y + height - 1));
    }

    // Map normalized (u,v) in [0,1] to integer pixel within the viewport.
    void MapNormalized(float u, float v, int& px, int& py) const {
        u = std::max(0.0f, std::min(1.0f, u));
        v = std::max(0.0f, std::min(1.0f, v));
        px = x + (int)(u * (float)width);
        py = y + (int)(v * (float)height);
        if (px >= x + width) px = x + width - 1;
        if (py >= y + height) py = y + height - 1;
    }
};

} // namespace bighero
