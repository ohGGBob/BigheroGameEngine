#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// NavGrid: a simple 2D navigation grid with walkable flags and helpers for
// A* style pathfinding (neighbour iteration, index conversion). Standard-
// library only, self-contained.
class NavGrid {
public:
    NavGrid() {}
    NavGrid(std::size_t w, std::size_t h, bool allWalkable = true)
        { Resize(w, h, allWalkable); }

    void Resize(std::size_t w, std::size_t h, bool allWalkable = true) {
        w_ = w < 1 ? 1 : w; h_ = h < 1 ? 1 : h;
        walkable_.assign(w_*h_, allWalkable ? 1 : 0);
    }
    std::size_t Width() const { return w_; }
    std::size_t Height() const { return h_; }

    bool IsWalkable(std::size_t x, std::size_t y) const {
        if (x >= w_ || y >= h_) return false;
        return walkable_[y*w_+x] != 0;
    }
    void SetWalkable(std::size_t x, std::size_t y, bool v) {
        if (x >= w_ || y >= h_) return;
        walkable_[y*w_+x] = v ? 1 : 0;
    }

    std::size_t Index(std::size_t x, std::size_t y) const {
        if (x >= w_ || y >= h_) return (std::size_t)-1;
        return y*w_ + x;
    }
    void XYOf(std::size_t idx, std::size_t& x, std::size_t& y) const {
        x = idx % w_; y = idx / w_;
    }

    // Collect up to 4 orthogonal neighbours that are walkable.
    void Neighbours(std::size_t idx, std::vector<std::size_t>& out) const {
        out.clear();
        if (idx == (std::size_t)-1 || idx >= w_*h_) return;
        std::size_t x, y; XYOf(idx, x, y);
        const int D[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (auto& d : D) {
            int nx = (int)x + d[0], ny = (int)y + d[1];
            if (nx < 0 || ny < 0 || nx >= (int)w_ || ny >= (int)h_) continue;
            std::size_t ni = (std::size_t)ny*w_ + (std::size_t)nx;
            if (walkable_[ni]) out.push_back(ni);
        }
    }

    void ClearAll() { for (auto& v : walkable_) v = 0; }
    void FillAll() { for (auto& v : walkable_) v = 1; }

private:
    std::size_t w_=1, h_=1;
    std::vector<unsigned char> walkable_;
};

} // namespace bighero
