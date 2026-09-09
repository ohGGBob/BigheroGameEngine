#pragma once
#include <vector>
#include <cstddef>
#include <cstdint>

namespace bighero {

// CellularAutomata: a settable 2D grid of cells evolving by a Moore-neighbour
// rule. Standard-library only, self-contained. Useful for procedural cave/noise
// generation and life-like simulations.
class CellularAutomata {
public:
    CellularAutomata() {}
    CellularAutomata(std::size_t w, std::size_t h) { Resize(w, h); }

    void Resize(std::size_t w, std::size_t h) {
        w_ = w < 1 ? 1 : w; h_ = h < 1 ? 1 : h;
        cells_.assign(w_ * h_, 0);
        flip_ = cells_;   // same size
    }

    std::size_t Width() const { return w_; }
    std::size_t Height() const { return h_; }

    bool Set(std::size_t x, std::size_t y, uint8_t v) {
        if (x >= w_ || y >= h_) return false;
        cells_[y * w_ + x] = v ? 1 : 0;
        return true;
    }
    bool Get(std::size_t x, std::size_t y) const {
        if (x >= w_ || y >= h_) return false;
        return cells_[y * w_ + x] != 0;
    }

    // Count alive Moore neighbours (8 neighbours).
    int NeighbourCount(std::size_t x, std::size_t y) const {
        int c = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = static_cast<int>(x) + dx, ny = static_cast<int>(y) + dy;
                if (nx < 0 || ny < 0 || nx >= static_cast<int>(w_) || ny >= static_cast<int>(h_)) continue;
                if (cells_[ny * w_ + nx]) ++c;
            }
        return c;
    }

    // One step with classical B3/S23 rule (Game of Life). Writes into the
    // flip buffer to avoid in-place contamination.
    void Step() {
        for (std::size_t y = 0; y < h_; ++y)
            for (std::size_t x = 0; x < w_; ++x) {
                int n = NeighbourCount(x, y);
                bool alive = Get(x, y);
                bool next = (alive && (n == 2 || n == 3)) || (!alive && n == 3);
                flip_[y * w_ + x] = next ? 1 : 0;
            }
        cells_.swap(flip_);
    }

    // Randomize with a given density.
    void Randomize(uint32_t seed, float density) {
        uint32_t s = seed ? seed : 1u;
        if (density < 0) density = 0;
        if (density > 1) density = 1;
        for (auto& c : cells_) {
            s = s * 1664525u + 1013904223u;
            float r = static_cast<float>(s % 10000u) / 10000.0f;
            c = (r < density) ? 1 : 0;
        }
    }

    std::size_t AliveCount() const {
        std::size_t c = 0;
        for (auto v : cells_) if (v) ++c;
        return c;
    }

private:
    std::size_t w_=1, h_=1;
    std::vector<uint8_t> cells_, flip_;
};

} // namespace bighero
