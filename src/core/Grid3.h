#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// Grid3: a rectangular 3D grid of cells (voxel-like) with a fixed cell size
// and an origin. Maps between world coordinates and cell indices.
// Self-contained, std-lib only.
class Grid3 {
public:
    Grid3() = default;
    Grid3(int width, int height, int depth, float cellSize,
          float originX = 0, float originY = 0, float originZ = 0)
        : width_(width), height_(height), depth_(depth), cellSize_(cellSize),
          originX_(originX), originY_(originY), originZ_(originZ) {
        cells_.assign((size_t)width_ * height_ * depth_, 0);
    }

    int Width() const { return width_; }
    int Height() const { return height_; }
    int Depth() const { return depth_; }
    float CellSize() const { return cellSize_; }

    int CellX(float wx) const {
        int x = (int)((wx - originX_) / cellSize_);
        return x < 0 ? 0 : (x >= width_ ? width_ - 1 : x);
    }
    int CellY(float wy) const {
        int y = (int)((wy - originY_) / cellSize_);
        return y < 0 ? 0 : (y >= height_ ? height_ - 1 : y);
    }
    int CellZ(float wz) const {
        int z = (int)((wz - originZ_) / cellSize_);
        return z < 0 ? 0 : (z >= depth_ ? depth_ - 1 : z);
    }

    float CenterX(int cx) const { return originX_ + (cx + 0.5f) * cellSize_; }
    float CenterY(int cy) const { return originY_ + (cy + 0.5f) * cellSize_; }
    float CenterZ(int cz) const { return originZ_ + (cz + 0.5f) * cellSize_; }

    bool InBounds(int cx, int cy, int cz) const {
        return cx >= 0 && cx < width_ && cy >= 0 && cy < height_ && cz >= 0 && cz < depth_;
    }

    float Get(int cx, int cy, int cz) const {
        if (!InBounds(cx, cy, cz)) return 0;
        return cells_[Index(cx, cy, cz)];
    }
    void Set(int cx, int cy, int cz, float v) {
        if (InBounds(cx, cy, cz)) cells_[Index(cx, cy, cz)] = v;
    }
    void Add(int cx, int cy, int cz, float v) {
        if (InBounds(cx, cy, cz)) cells_[Index(cx, cy, cz)] += v;
    }

    void Clear(float v = 0) { std::fill(cells_.begin(), cells_.end(), v); }
    size_t TotalCells() const { return cells_.size(); }

private:
    size_t Index(int cx, int cy, int cz) const {
        return ((size_t)cz * height_ + cy) * width_ + cx;
    }
    int width_ = 0, height_ = 0, depth_ = 0;
    float cellSize_ = 1.0f;
    float originX_ = 0, originY_ = 0, originZ_ = 0;
    std::vector<float> cells_;
};

} // namespace bighero
