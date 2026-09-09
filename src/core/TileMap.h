#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>

namespace bighero {

// TileMap: a compact 2D tile grid storage with fast get/set, row padding,
// and simple copy helpers. Designed for level/tilemap data; tiles are opaque
// integer ids. Standard-library only, self-contained.
class TileMap {
public:
    TileMap() = default;
    TileMap(int32_t w, int32_t h) { Resize(w, h); }

    void Resize(int32_t w, int32_t h) {
        w_ = w < 0 ? 0 : w;
        h_ = h < 0 ? 0 : h;
        data_.assign((size_t)w_ * h_, 0);
    }
    int32_t Width() const { return w_; }
    int32_t Height() const { return h_; }

    bool InBounds(int32_t x, int32_t y) const {
        return x >= 0 && x < w_ && y >= 0 && y < h_;
    }
    uint32_t Get(int32_t x, int32_t y) const {
        if (!InBounds(x, y)) return 0;
        return data_[(size_t)y * w_ + x];
    }
    void Set(int32_t x, int32_t y, uint32_t tile) {
        if (!InBounds(x, y)) return;
        data_[(size_t)y * w_ + x] = tile;
    }
    void Fill(uint32_t tile) { std::fill(data_.begin(), data_.end(), tile); }
    void Clear() { std::fill(data_.begin(), data_.end(), 0); }
    size_t CellCount() const { return data_.size(); }

    // Count occurrences of a particular tile id.
    size_t CountTile(uint32_t tile) const {
        size_t c = 0;
        for (uint32_t t : data_) if (t == tile) ++c;
        return c;
    }

private:
    int32_t w_ = 0, h_ = 0;
    std::vector<uint32_t> data_;
};

} // namespace bighero
