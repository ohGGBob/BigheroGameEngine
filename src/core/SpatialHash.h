#pragma once
#include <vector>
#include <unordered_map>
#include <cstddef>
#include <cmath>

namespace bighero {

// SpatialHash: a uniform-hash spatial grid for 2D broad-phase queries. Maps
// cell keys to lists of entity ids. Standard-library only, self-contained.
class SpatialHash {
public:
    SpatialHash() {}
    SpatialHash(float cellSize) : cellSize_(cellSize > 1e-6f ? cellSize : 1e-6f) {}

    void SetCellSize(float s) { cellSize_ = s > 1e-6f ? s : 1e-6f; }
    float CellSize() const { return cellSize_; }

    void Clear() { cells_.clear(); }

    // Insert an id at (x,y).
    void Insert(unsigned id, float x, float y) {
        long key = KeyOf(x, y);
        cells_[key].push_back(id);
    }

    // Collect all ids within radius r of (x,y) (broad phase; may include
    // candidates whose true distance > r).
    void Query(float x, float y, float r, std::vector<unsigned>& out) const {
        out.clear();
        int minCx = cell(x - r), maxCx = cell(x + r);
        int minCy = cell(y - r), maxCy = cell(y + r);
        std::unordered_map<unsigned, bool> seen;
        for (int cy = minCy; cy <= maxCy; ++cy)
            for (int cx = minCx; cx <= maxCx; ++cx) {
                auto it = cells_.find(Key(cx, cy));
                if (it == cells_.end()) continue;
                for (unsigned id : it->second) {
                    if (seen.find(id) == seen.end()) {
                        seen[id] = true;
                        out.push_back(id);
                    }
                }
            }
    }

    void Remove(unsigned id, float x, float y) {
        long key = KeyOf(x, y);
        auto it = cells_.find(key);
        if (it == cells_.end()) return;
        auto& v = it->second;
        for (std::size_t i=0;i<v.size();++i)
            if (v[i]==id) { v[i]=v.back(); v.pop_back(); return; }
    }

private:
    int cell(float v) const {
        int c = (int)std::floor(v / cellSize_);
        return c;
    }
    static long Key(int cx, int cy) {
        return ((long)cx << 32) ^ (unsigned)cy;
    }
    long KeyOf(float x, float y) const { return Key(cell(x), cell(y)); }

    float cellSize_ = 1.0f;
    std::unordered_map<long, std::vector<unsigned>> cells_;
};

} // namespace bighero
