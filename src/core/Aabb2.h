#pragma once
#include <algorithm>

namespace bighero {

// 2D axis-aligned bounding box.
struct Aabb2 {
    float minx, miny, maxx, maxy;

    Aabb2() : minx(0), miny(0), maxx(0), maxy(0) {}

    Aabb2(float minx_, float miny_, float maxx_, float maxy_)
        : minx(minx_), miny(miny_), maxx(maxx_), maxy(maxy_) {}

    static Aabb2 FromCenterSize(float cx, float cy, float w, float h) {
        return Aabb2(cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f);
    }

    bool Contains(float px, float py) const {
        return px >= minx && px <= maxx && py >= miny && py <= maxy;
    }

    bool Overlaps(const Aabb2& o) const {
        return !(o.minx > maxx || o.maxx < minx || o.miny > maxy || o.maxy < miny);
    }

    bool ContainsAabb(const Aabb2& o) const {
        return o.minx >= minx && o.maxx <= maxx && o.miny >= miny && o.maxy <= maxy;
    }

    void ExpandToInclude(const Aabb2& o) {
        minx = std::min(minx, o.minx);
        miny = std::min(miny, o.miny);
        maxx = std::max(maxx, o.maxx);
        maxy = std::max(maxy, o.maxy);
    }

    float Width() const { return maxx - minx; }
    float Height() const { return maxy - miny; }
};

} // namespace bighero
