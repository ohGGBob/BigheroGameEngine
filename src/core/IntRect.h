#pragma once

namespace bighero {

// Integer rectangle (inclusive pixel coordinates).
struct IntRect {
    int x, y, w, h;

    IntRect() : x(0), y(0), w(0), h(0) {}
    IntRect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}

    int Right() const { return x + w; }
    int Bottom() const { return y + h; }
    bool Empty() const { return w <= 0 || h <= 0; }

    bool Contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    bool Intersects(const IntRect& o) const {
        return !(o.x >= x + w || o.x + o.w <= x || o.y >= y + h || o.y + o.h <= y);
    }
};

} // namespace bighero
