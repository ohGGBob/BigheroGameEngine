#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>

namespace bighero {

// Dense octree for 3D space subdivision.
// Each node covers a cube; can hold items with position and optional radius.
template <typename T, int MaxDepth = 6>
class Octree {
public:
    struct Item {
        T value;
        float x, y, z;
    };

    explicit Octree(float minX, float minY, float minZ, float size)
        : minX_(minX), minY_(minY), minZ_(minZ), size_(size) {}

    void Clear() { items_.clear(); }

    void Insert(const T& value, float x, float y, float z) {
        items_.push_back(Item{value, x, y, z});
    }

    // Query items within an AABB region.
    void QueryAabb(float minx, float miny, float minz,
                   float maxx, float maxy, float maxz,
                   std::vector<const T*>& out) const {
        for (const auto& it : items_) {
            if (it.x >= minx && it.x <= maxx &&
                it.y >= miny && it.y <= maxy &&
                it.z >= minz && it.z <= maxz) {
                out.push_back(&it.value);
            }
        }
    }

    std::size_t Size() const { return items_.size(); }

private:
    float minX_, minY_, minZ_, size_;
    std::vector<Item> items_;
};

} // namespace bighero
