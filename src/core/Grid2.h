#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// Grid2: a rectangular 2D grid of cells with a fixed cell size and an origin.
// Maps between world coordinates and cell indices and stores per-cell values.
// Self-contained, std-lib only.
class Grid2 {
public:
    Grid2() = default;
    Grid2(int width, int height, float cellSize, float originX = 0, float originY = 0)
        : width_(width), height_(height), cellSize_(cellSize),
          originX_(originX), originY_(originY) {
        cells_.assign((size_t)width_ * (size_t)height_, 0);
    }

    int Width() const { return width_; }
    int Height() const { return height_; }
    float CellSize() const { return cellSize_; }

    // World coordinate -> cell index (clamped to grid bounds).
    int CellX(float wx) const {
        int x = (int)((wx - originX_) / cellSize_);
        return x < 0 ? 0 : (x >= width_ ? width_ - 1 : x);
    }
    int CellY(float wy) const {
        int y = (int)((wy - originY_) / cellSize_);
        return y < 0 ? 0 : (y >= height_ ? height_ - 1 : y);
    }

    // Cell index -> world coordinate of the cell's center.
    float CenterX(int cx) const { return originX_ + (cx + 0.5f) * cellSize_; }
    float CenterY(int cy) const { return originY_ + (cy + 0.5f) * cellSize_; }

    bool InBounds(int cx, int cy) const {
        return cx >= 0 && cx < width_ && cy >= 0 && cy < height_;
    }

    float Get(int cx, int cy) const {
        if (!InBounds(cx, cy)) return 0;
        return cells_[(size_t)cy * width_ + cx];
    }
    void Set(int cx, int cy, float v) {
        if (InBounds(cx, cy)) cells_[(size_t)cy * width_ + cx] = v;
    }
    void Add(int cx, int cy, float v) {
        if (InBounds(cx, cy)) cells_[(size_t)cy * width_ + cx] += v;
    }

    void Clear(float v = 0) { std::fill(cells_.begin(), cells_.end(), v); }
    size_t TotalCells() const { return cells_.size(); }
    bool Empty() const { return cells_.empty(); }

private:
    int width_ = 0, height_ = 0;
    float cellSize_ = 1.0f;
    float originX_ = 0, originY_ = 0;
    std::vector<float> cells_;
};

} // namespace bighero
