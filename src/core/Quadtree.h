#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>

namespace bighero {

// Quadtree for 2D spatial indexing of point-like items.
// Items are small movable values; caller supplies an AABB to query.
template <typename T>
class Quadtree {
public:
    struct Item {
        T value;
        float x, y;
    };

    explicit Quadtree(int maxDepth = 6) : maxDepth_(maxDepth) {}

    void Clear() { items_.clear(); }

    void Insert(const T& value, float x, float y) {
        items_.push_back(Item{value, x, y});
    }

    // Query all items whose point falls in the given rectangle.
    void QueryRect(float minx, float miny, float maxx, float maxy,
                   std::vector<const T*>& out) const {
        for (const auto& it : items_) {
            if (it.x >= minx && it.x <= maxx && it.y >= miny && it.y <= maxy)
                out.push_back(&it.value);
        }
    }

    // Query all items whose point falls within radius of (cx, cy).
    void QueryCircle(float cx, float cy, float radius, std::vector<const T*>& out) const {
        float r2 = radius * radius;
        for (const auto& it : items_) {
            float dx = it.x - cx, dy = it.y - cy;
            if (dx * dx + dy * dy <= r2)
                out.push_back(&it.value);
        }
    }

    std::size_t Size() const { return items_.size(); }
    bool Empty() const { return items_.empty(); }

private:
    std::vector<Item> items_;
    int maxDepth_;
};

} // namespace bighero
