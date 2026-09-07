#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <climits>
#include <cmath>

namespace bighero {

// Uniform 2D spatial grid for broad-phase queries over point/box objects.
class UniformGrid2D {
public:
    struct Cell { float x, y, w, h; int id; };
    struct CellRange { int minX, minY, maxX, maxY; };

    UniformGrid2D() : cellSize_(1.0f) {}
    explicit UniformGrid2D(float cellSize) : cellSize_(cellSize > 0 ? cellSize : 1.0f) {}

    // Rebuild grid from items.
    void Build(const std::vector<Cell>& items) {
        items_ = items;
        cells_.assign(0, {});
        minX_ = minY_ = 0;
        maxX_ = maxY_ = -1;
        if (items_.empty()) return;
        // compute overall bounds
        minX_ = INT_MAX; minY_ = INT_MAX; maxX_ = INT_MIN; maxY_ = INT_MIN;
        for (auto& it : items_) {
            int cx0 = CellOfX(it.x), cy0 = CellOfY(it.y);
            int cx1 = CellOfX(it.x + it.w), cy1 = CellOfY(it.y + it.h);
            if (cx0 < minX_) minX_ = cx0; if (cy0 < minY_) minY_ = cy0;
            if (cx1 > maxX_) maxX_ = cx1; if (cy1 > maxY_) maxY_ = cy1;
        }
        int gw = maxX_ - minX_ + 1, gh = maxY_ - minY_ + 1;
        if (gw <= 0 || gh <= 0) return;
        cells_.assign((size_t)gw * gh, {});
        for (size_t i = 0; i < items_.size(); ++i) {
            const Cell& it = items_[i];
            int cx0 = CellOfX(it.x), cy0 = CellOfY(it.y);
            int cx1 = CellOfX(it.x + it.w), cy1 = CellOfY(it.y + it.h);
            for (int cy = cy0; cy <= cy1; ++cy)
                for (int cx = cx0; cx <= cx1; ++cx)
                    cells_[(size_t)(cy - minY_) * gw + (cx - minX_)].push_back((int)i);
        }
        gridW_ = gw; gridH_ = gh;
    }

    // Query which item ids overlap the given box (unique ids).
    void Query(float x, float y, float w, float h, std::vector<int>& out) const {
        out.clear();
        if (cells_.empty()) return;
        mark_.assign(items_.size(), 0);
        ++stamp_;
        if (stamp_ == 0) { mark_.assign(items_.size(), 0); stamp_ = 1; }
        int cx0 = CellOfX(x), cy0 = CellOfY(y);
        int cx1 = CellOfX(x + w), cy1 = CellOfY(y + h);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx)
                for (int id : cells_[(size_t)(cy - minY_) * gridW_ + (cx - minX_)])
                    if (mark_[id] != stamp_ && Overlaps(items_[id], x, y, w, h)) {
                        mark_[id] = stamp_;
                        out.push_back(id);
                    }
    }

private:
    int CellOfX(float x) const { return (int)std::floor(x / cellSize_); }
    int CellOfY(float y) const { return (int)std::floor(y / cellSize_); }
    static bool Overlaps(const Cell& a, float x, float y, float w, float h) {
        return a.x < x + w && x < a.x + a.w && a.y < y + h && y < a.y + a.h;
    }
    float cellSize_;
    std::vector<Cell> items_;
    std::vector<std::vector<int>> cells_;
    int minX_, minY_, maxX_, maxY_;
    int gridW_ = 0, gridH_ = 0;
    mutable std::vector<int> mark_;
    mutable int stamp_ = 0;
};

} // namespace bighero
