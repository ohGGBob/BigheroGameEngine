#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// Simple 2D tilemap container with tile id storage and queries.
class Tilemap2D {
public:
    Tilemap2D() : width_(0), height_(0) {}
    Tilemap2D(int w, int h, int defaultTile = 0)
        : width_(w), height_(h), tiles_((size_t)w * h, defaultTile) {}

    void Resize(int w, int h, int defaultTile = 0) {
        width_ = w; height_ = h;
        tiles_.assign((size_t)w * h, defaultTile);
    }
    int Width() const { return width_; }
    int Height() const { return height_; }

    bool InBounds(int x, int y) const { return x >= 0 && x < width_ && y >= 0 && y < height_; }

    void SetTile(int x, int y, int tile) {
        if (InBounds(x, y)) tiles_[(size_t)y * width_ + x] = tile;
    }
    int GetTile(int x, int y) const {
        if (!InBounds(x, y)) return 0;
        return tiles_[(size_t)y * width_ + x];
    }

    // Replace all occurrences of oldTile with newTile.
    void ReplaceTile(int oldTile, int newTile) {
        for (auto& t : tiles_) if (t == oldTile) t = newTile;
    }

    // Count occurrences of a tile id.
    int CountTile(int tile) const {
        int c = 0; for (auto t : tiles_) if (t == tile) ++c; return c;
    }

    void Clear(int tile = 0) { tiles_.assign(tiles_.size(), tile); }

    // Iterate all solid tile positions of a given id.
    void CollectPositions(int tile, std::vector<std::pair<int,int>>& out) const {
        out.clear();
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
                if (GetTile(x,y) == tile) out.push_back({x,y});
    }

private:
    int width_, height_;
    std::vector<int> tiles_;
};

} // namespace bighero
