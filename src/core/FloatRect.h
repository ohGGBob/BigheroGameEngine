#pragma once

namespace bighero {

// FloatRect: a rectangle with float coordinates, supporting containment,
// intersection, and point/center/size queries. Self-contained.
struct FloatRect {
    float x = 0, y = 0, w = 0, h = 0;

    FloatRect() = default;
    FloatRect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}

    float xMin() const { return x; }
    float yMin() const { return y; }
    float xMax() const { return x + w; }
    float yMax() const { return y + h; }

    float CenterX() const { return x + w * 0.5f; }
    float CenterY() const { return y + h * 0.5f; }

    bool Contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    bool Overlaps(const FloatRect& o) const {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
    // Intersection rectangle; returns false if they do not overlap.
    bool Intersect(const FloatRect& o, FloatRect& out) const {
        float nx = x > o.x ? x : o.x;
        float ny = y > o.y ? y : o.y;
        float nx2 = (x + w) < (o.x + o.w) ? (x + w) : (o.x + o.w);
        float ny2 = (y + h) < (o.y + o.h) ? (y + h) : (o.y + o.h);
        if (nx2 <= nx || ny2 <= ny) return false;
        out = FloatRect(nx, ny, nx2 - nx, ny2 - ny);
        return true;
    }
    void Expand(float amount) {
        x -= amount; y -= amount; w += amount * 2; h += amount * 2;
    }
};

} // namespace bighero
