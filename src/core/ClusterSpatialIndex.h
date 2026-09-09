#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// ClusterSpatialIndex: a uniform-grid spatial index for broad-phase queries.
// Buckets AABBs into cells by their center; answers overlap queries. Pure
// CPU-side, self-contained, standard-library only.
class ClusterSpatialIndex {
public:
    ClusterSpatialIndex() {}
    ClusterSpatialIndex(float cellSize, std::size_t width, std::size_t height)
        : cellSize_(cellSize < 1e-4f ? 1e-4f : cellSize),
          width_(width < 1 ? 1 : width),
          height_(height < 1 ? 1 : height) {
        cells_.assign(width_*height_, {});
    }

    void Resize(float cellSize, std::size_t width, std::size_t height) {
        cellSize_ = cellSize < 1e-4f ? 1e-4f : cellSize;
        width_ = width < 1 ? 1 : width;
        height_ = height < 1 ? 1 : height;
        cells_.assign(width_*height_, {});
    }

    // Insert an item id whose center is at (x,y).
    void Insert(std::size_t id, float x, float y) {
        std::size_t cx = cellX(x), cy = cellY(y);
        cells_[cy*width_ + cx].push_back(id);
    }
    std::size_t CellCount() const { return width_*height_; }

    // Query all ids in a circular region.
    std::size_t Query(float x, float y, float radius, std::vector<std::size_t>& out) const {
        out.clear();
        std::size_t cx = cellX(x), cy = cellY(y);
        std::size_t r = (std::size_t)std::ceil(radius / cellSize_);
        for (std::size_t dy = (cy > r ? cy-r : 0); ; ++dy) {
            if (dy > cy + r || dy >= height_) break;
            for (std::size_t dx = (cx > r ? cx-r : 0); ; ++dx) {
                if (dx > cx + r || dx >= width_) break;
                for (auto id : cells_[dy*width_ + dx]) out.push_back(id);
            }
        }
        return out.size();
    }

    std::size_t ItemCount() const {
        std::size_t n = 0;
        for (auto& c : cells_) n += c.size();
        return n;
    }
    void Clear() { for (auto& c : cells_) c.clear(); }

private:
    std::size_t cellX(float x) const {
        std::size_t cx = (std::size_t)(x / cellSize_);
        return cx < width_ ? cx : width_-1;
    }
    std::size_t cellY(float y) const {
        std::size_t cy = (std::size_t)(y / cellSize_);
        return cy < height_ ? cy : height_-1;
    }
    float cellSize_ = 1.0f;
    std::size_t width_ = 1, height_ = 1;
    std::vector<std::vector<std::size_t>> cells_;
};

} // namespace bighero
