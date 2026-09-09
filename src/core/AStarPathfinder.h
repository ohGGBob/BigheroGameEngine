#pragma once
#include <vector>
#include <cstddef>
#include <utility>

namespace bighero {

// AStarPathfinder: a plain 2D grid A* path scaffolder. Uses standard-library
// vectors (no external priority queue) for self-containment. Returns a
// walkable path of grid cell indices from start to goal. Standard-library only.
class AStarPathfinder {
public:
    AStarPathfinder() {}
    AStarPathfinder(std::size_t w, std::size_t h, const std::vector<bool>& walkable)
        : w_(w), h_(h), walkable_(walkable) {}

    void SetGrid(std::size_t w, std::size_t h, const std::vector<bool>& walkable) {
        w_=w; h_=h; walkable_=walkable;
    }
    std::size_t Width() const { return w_; }
    std::size_t Height() const { return h_; }

    bool IsWalkable(std::size_t x, std::size_t y) const {
        if (x>=w_ || y>=h_) return false;
        return walkable_[y*w_+x];
    }
    void SetWalkable(std::size_t x, std::size_t y, bool v) {
        if (x>=w_ || y>=h_) return;
        walkable_[y*w_+x]=v;
    }

    // Find a path from (sx,sy) to (gx,gy). Fills path with cell indices.
    bool FindPath(std::size_t sx, std::size_t sy, std::size_t gx, std::size_t gy,
                  std::vector<std::size_t>& path) const {
        path.clear();
        if (sx>=w_||sy>=h_||gx>=w_||gy>=h_) return false;
        if (!IsWalkable(sx,sy) || !IsWalkable(gx,gy)) return false;
        if (sx==gx && sy==gy) { path.push_back(sy*w_+sx); return true; }
        std::size_t n = w_*h_;
        std::vector<float> g(n, 1e30f);
        std::vector<float> f(n, 1e30f);
        std::vector<long> came(n, -1);
        std::vector<bool> closed(n, false);
        std::vector<std::size_t> open;
        auto push = [&](std::size_t idx){ 
            for (std::size_t i=0;i<open.size();++i) if (open[i]==idx) return; 
            open.push_back(idx); 
        };
        g[sy*w_+sx]=0;
        f[sy*w_+sx] = H(sx,sy,gx,gy);
        push(sy*w_+sx);
        bool found=false;
        while (!open.empty()) {
            // find lowest f in open
            std::size_t bi=0;
            for (std::size_t i=1;i<open.size();++i) if (f[open[i]]<f[open[bi]]) bi=i;
            std::size_t cur=open[bi];
            if (cur == gy*w_+gx) { found=true; break; }
            open[bi]=open.back(); open.pop_back();
            closed[cur]=true;
            int cx=(int)(cur%w_), cy=(int)(cur/w_);
            const int D[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (auto&d:D) {
                int nx=cx+d[0], ny=cy+d[1];
                if (nx<0||ny<0||nx>=(int)w_||ny>=(int)h_) continue;
                std::size_t ni=(std::size_t)ny*w_+(std::size_t)nx;
                if (!walkable_[ni] || closed[ni]) continue;
                float tentative = g[cur] + 1.0f;
                if (tentative < g[ni]) {
                    g[ni]=tentative; came[ni]=(long)cur;
                    f[ni]=tentative + H((std::size_t)nx,(std::size_t)ny,gx,gy);
                    push(ni);
                }
            }
        }
        if (!found) return false;
        std::size_t cur=gy*w_+gx;
        std::vector<std::size_t> rev;
        while (came[cur]!=-1) { rev.push_back(cur); cur=(std::size_t)came[cur]; }
        rev.push_back(sy*w_+sx);
        for (std::size_t i=rev.size();i>0;--i) path.push_back(rev[i-1]);
        return true;
    }

private:
    static float H(std::size_t x, std::size_t y, std::size_t gx, std::size_t gy) {
        long dx=(long)x-(long)gx, dy=(long)y-(long)gy;
        return (float)(std::fabs(dx)+std::fabs(dy));
    }
    std::size_t w_=1, h_=1;
    std::vector<bool> walkable_;
};

} // namespace bighero
