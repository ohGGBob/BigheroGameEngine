#pragma once
#include <vector>
#include <queue>
#include <cmath>
#include <limits>
#include <algorithm>

namespace bighero {

// Grid-based A* pathfinding over a uniform 2D grid.
struct AStar2D {
    int width, height;
    // occupancy[y*width + x] == true means walkable
    std::vector<bool> walkable;
    // 4-directional by default; set diagonal to true for 8-directional
    bool diagonal = false;

    AStar2D() : width(0), height(0) {}
    AStar2D(int w, int h, bool walkableDefault = true)
        : width(w), height(h), walkable((size_t)w * h, walkableDefault) {}

    void SetWalkable(int x, int y, bool v) {
        if (x < 0 || x >= width || y < 0 || y >= height) return;
        walkable[(size_t)y * width + x] = v;
    }
    bool IsWalkable(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return false;
        return walkable[(size_t)y * width + x];
    }

    // Returns a path of grid cells (start -> goal) excluding start, or empty if none.
    std::vector<std::pair<int,int>> FindPath(int sx, int sy, int gx, int gy) const {
        std::vector<std::pair<int,int>> path;
        if (!IsWalkable(sx, sy) || !IsWalkable(gx, gy)) return path;
        if (sx == gx && sy == gy) return path;

        const int N = width * height;
        std::vector<float> g(N, std::numeric_limits<float>::infinity());
        std::vector<int> came(N, -1);
        std::vector<bool> closed(N, false);

        auto idx = [&](int x, int y) { return y * width + x; };
        auto hcost = [&](int x, int y) {
            float dx = (float)(x - gx), dy = (float)(y - gy);
            return std::sqrt(dx * dx + dy * dy);
        };
        struct Q { float f; int id; };
        auto cmp = [](const Q& a, const Q& b) { return a.f > b.f; };
        std::priority_queue<Q, std::vector<Q>, decltype(cmp)> open(cmp);

        int start = idx(sx, sy);
        g[start] = 0.0f;
        open.push({ hcost(sx, sy), start });

        const int dir4[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        const int dir8[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
        const int (*dirs)[2] = diagonal ? dir8 : dir4;
        const int ndir = diagonal ? 8 : 4;
        const float diagCost = std::sqrt(2.0f);

        while (!open.empty()) {
            Q cur = open.top(); open.pop();
            int cid = cur.id;
            if (closed[cid]) continue;
            closed[cid] = true;
            int cx = cid % width, cy = cid / width;
            if (cx == gx && cy == gy) break;
            for (int d = 0; d < ndir; ++d) {
                int nx = cx + dirs[d][0], ny = cy + dirs[d][1];
                if (!IsWalkable(nx, ny)) continue;
                if (diagonal && dirs[d][0] != 0 && dirs[d][1] != 0) {
                    if (!IsWalkable(cx + dirs[d][0], cy) || !IsWalkable(cx, cy + dirs[d][1]))
                        continue; // no corner cutting
                }
                int nid = idx(nx, ny);
                if (closed[nid]) continue;
                float step = (dirs[d][0] != 0 && dirs[d][1] != 0) ? diagCost : 1.0f;
                float ng = g[cid] + step;
                if (ng < g[nid]) {
                    g[nid] = ng;
                    came[nid] = cid;
                    open.push({ ng + hcost(nx, ny), nid });
                }
            }
        }
        int goal = idx(gx, gy);
        if (came[goal] == -1) return path; // unreachable
        int cur = goal;
        while (cur != -1 && cur != start) {
            path.push_back({ cur % width, cur / width });
            cur = came[cur];
        }
        std::reverse(path.begin(), path.end());
        return path;
    }
};

} // namespace bighero
