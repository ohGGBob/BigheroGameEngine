#pragma once
#include "Vector2Int.h"

namespace bighero {

// RectInt: an integer rectangle in 2D space with overlap/containment helpers.
// Self-contained, depends only on Vector2Int.
struct RectInt {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    RectInt() = default;
    RectInt(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}

    int xMin() const { return x; }
    int yMin() const { return y; }
    int xMax() const { return x + w; }
    int yMax() const { return y + h; }

    Vector2Int Center() const { return { x + w / 2, y + h / 2 }; }
    Vector2Int Position() const { return { x, y }; }
    Vector2Int Size() const { return { w, h }; }

    bool Contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    bool Contains(const Vector2Int& p) const { return Contains(p.x, p.y); }
    bool Overlaps(const RectInt& o) const {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
    static RectInt MinMaxRect(int xmin, int ymin, int xmax, int ymax) {
        return { xmin, ymin, xmax - xmin, ymax - ymin };
    }
};

} // namespace bighero
