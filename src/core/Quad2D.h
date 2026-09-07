#pragma once
#include <algorithm>

namespace bighero {

// Axis-aligned quad (2D rectangle) with min/max corners.
struct Quad2D {
    float minx, miny, maxx, maxy;

    Quad2D() : minx(0), miny(0), maxx(0), maxy(0) {}
    Quad2D(float minx_, float miny_, float maxx_, float maxy_)
        : minx(minx_), miny(miny_), maxx(maxx_), maxy(maxy_) {}

    bool Contains(float px, float py) const {
        return px >= minx && px <= maxx && py >= miny && py <= maxy;
    }

    bool Overlaps(const Quad2D& o) const {
        return !(o.minx > maxx || o.maxx < minx || o.miny > maxy || o.maxy < miny);
    }

    void ExpandToInclude(const Quad2D& o) {
        minx = std::min(minx, o.minx);
        miny = std::min(miny, o.miny);
        maxx = std::max(maxx, o.maxx);
        maxy = std::max(maxy, o.maxy);
    }

    float Width() const { return maxx - minx; }
    float Height() const { return maxy - miny; }
    float Area() const { return Width() * Height(); }
};

} // namespace bighero
