#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace bighero {

// 2D grid based pathfinding (A* on a tile grid) with 4- or 8-directional
// movement and optional diagonal cost weighting.
class GridPathfinding {
public:
    explicit GridPathfinding(int w, int h)
        : w_(w), h_(h), walkable_((std::size_t)w * h, true) {}

    void SetWalkable(int x, int y, bool ok) {
        if (InBounds(x, y)) walkable_[Index(x, y)] = ok;
    }
    bool IsWalkable(int x, int y) const {
        return InBounds(x, y) && walkable_[Index(x, y)];
    }
    int Width() const { return w_; }
    int Height() const { return h_; }

    // A* path from (sx,sy) to (tx,ty). Returns empty if unreachable.
    // 8-dir if `diagonal` else 4-dir.
    std::vector<std::pair<int,int>> FindPath(int sx, int sy, int tx, int ty,
                                             bool diagonal = true) const {
        std::vector<std::pair<int,int>> path;
        if (!InBounds(sx, sy) || !InBounds(tx, ty)) return path;
        if (!IsWalkable(sx, sy) || !IsWalkable(tx, ty)) return path;

        const std::size_t n = (std::size_t)w_ * h_;
        std::vector<float> g(n, 1e30f);
        std::vector<float> f(n, 1e30f);
        std::vector<int> came(n, -1);
        std::vector<char> closed(n, 0);

        struct QE { int idx; float f; };
        auto cmp = [](const QE& a, const QE& b){ return a.f > b.f; };
        std::vector<QE> open;
        auto heap_push = [&](int idx, float fv){ open.push_back({idx, fv}); std::push_heap(open.begin(), open.end(), cmp); };
        auto heap_pop = [&](){ std::pop_heap(open.begin(), open.end(), cmp); QE t = open.back(); open.pop_back(); return t; };

        int start = Index(sx, sy), goal = Index(tx, ty);
        g[start] = 0;
        f[start] = Heuristic(sx, sy, tx, ty);
        heap_push(start, f[start]);

        const int dx[8] = {1,-1,0,0,1,1,-1,-1};
        const int dy[8] = {0,0,1,-1,1,-1,1,-1};

        while (!open.empty()) {
            QE cur = heap_pop();
            int ci = cur.idx;
            if (closed[ci]) continue;
            if (ci == goal) {
                // reconstruct
                int p = goal;
                while (p != -1) { path.push_back({p % w_, p / w_}); p = came[p]; }
                std::size_t lo = 0, hi = path.size();
                while (lo < hi) { --hi; std::swap(path[lo], path[hi]); ++lo; }
                if (!path.empty() && path.back().first == sx && path.back().second == sy) {}
                return path;
            }
            closed[ci] = 1;
            int cx = ci % w_, cy = ci / w_;
            int dirs = diagonal ? 8 : 4;
            for (int d = 0; d < dirs; ++d) {
                int nx = cx + dx[d], ny = cy + dy[d];
                if (!IsWalkable(nx, ny)) continue;
                float ng = g[ci] + ((d >= 4) ? 1.41421356f : 1.0f);
                int ni = Index(nx, ny);
                if (ng < g[ni]) {
                    g[ni] = ng;
                    f[ni] = ng + Heuristic(nx, ny, tx, ty);
                    came[ni] = ci;
                    heap_push(ni, f[ni]);
                }
            }
        }
        return path; // empty -> unreachable
    }

private:
    bool InBounds(int x, int y) const { return x >= 0 && x < w_ && y >= 0 && y < h_; }
    std::size_t Index(int x, int y) const { return (std::size_t)y * w_ + x; }
    float Heuristic(int x, int y, int tx, int ty) const {
        int dx = std::abs(x - tx), dy = std::abs(y - ty);
        // octile distance
        return std::max(dx, dy) + 0.41421356f * std::min(dx, dy);
    }
    int w_, h_;
    std::vector<bool> walkable_;
};

} // namespace bighero
