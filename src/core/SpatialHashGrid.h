#pragma once
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace bighero {

// Uniform spatial hash grid for 2D broad-phase queries.
// Buckets keyed by integer cell coordinates; items store position.
template <typename T>
class SpatialHashGrid {
public:
    explicit SpatialHashGrid(float cellSize) : cellSize_(cellSize > 0 ? cellSize : 1.0f) {}

    void Clear() { cells_.clear(); }

    void Insert(const T& value, float x, float y) {
        int cx = CellCoord(x), cy = CellCoord(y);
        cells_[Key(cx, cy)].push_back(Item{value, x, y});
    }

    // Query all items within the same cell and adjacent cells around (cx, cy).
    void QueryRadius(float x, float y, float radius, std::vector<const T*>& out) const {
        int minCx = CellCoord(x - radius), maxCx = CellCoord(x + radius);
        int minCy = CellCoord(y - radius), maxCy = CellCoord(y + radius);
        float r2 = radius * radius;
        for (int cx = minCx; cx <= maxCx; ++cx) {
            for (int cy = minCy; cy <= maxCy; ++cy) {
                auto it = cells_.find(Key(cx, cy));
                if (it == cells_.end()) continue;
                for (const auto& item : it->second) {
                    float dx = item.x - x, dy = item.y - y;
                    if (dx * dx + dy * dy <= r2)
                        out.push_back(&item.value);
                }
            }
        }
    }

    std::size_t Size() const {
        std::size_t n = 0;
        for (const auto& kv : cells_) n += kv.second.size();
        return n;
    }

private:
    int CellCoord(float v) const { return (int)std::floor(v / cellSize_); }
    static std::uint64_t Key(int cx, int cy) {
        return ((std::uint64_t)(std::uint32_t)cx << 32) | (std::uint32_t)cy;
    }

    struct Item { T value; float x, y; };
    float cellSize_;
    std::unordered_map<std::uint64_t, std::vector<Item>> cells_;
};

} // namespace bighero
