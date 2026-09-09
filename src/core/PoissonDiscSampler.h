#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace bighero {

// PoissonDiscSampler: a 2D Poisson-disc (blue-noise) point sampler using the
// classic Bridson 'best candidate' grid-based method. Standard-library only,
// self-contained.
class PoissonDiscSampler {
public:
    PoissonDiscSampler() {}
    PoissonDiscSampler(float width, float height, float radius, uint32_t seed = 1337u)
        { Generate(width, height, radius, seed); }

    void Generate(float width, float height, float radius, uint32_t seed) {
        points_.clear();
        if (radius <= 0) return;
        w_ = width; h_ = height; r_ = radius;
        cell_ = r_ / std::sqrt(2.0f);
        gw_ = static_cast<int>(std::ceil(w_ / cell_));
        gh_ = static_cast<int>(std::ceil(h_ / cell_));
        if (gw_ < 1) gw_ = 1;
        if (gh_ < 1) gh_ = 1;
        grid_.assign(static_cast<std::size_t>(gw_ * gh_), -1);

        uint32_t s = seed ? seed : 1u;
        // Start with a random point.
        float sx = Rand(s) * w_, sy = Rand(s) * h_;
        AddPoint(sx, sy);
        active_.push_back(0);

        while (!active_.empty()) {
            // Pick a random active point index (Bridson).
            s = s * 1664525u + 1013904223u;
            int ai = static_cast<int>(s % static_cast<uint32_t>(active_.size()));
            std::size_t activeIdx = active_[ai];
            float px = points_[activeIdx].x, py = points_[activeIdx].y;
            bool found = false;
            for (int k = 0; k < 30; ++k) {
                float ang = Rand(s) * 6.2831853f;
                float rad = r_ * (1.0f + Rand(s));
                float nx = px + std::cos(ang) * rad;
                float ny = py + std::sin(ang) * rad;
                if (nx < 0 || ny < 0 || nx >= w_ || ny >= h_) continue;
                if (HasNear(nx, ny)) continue;
                AddPoint(nx, ny);
                active_.push_back(static_cast<std::size_t>(points_.size() - 1));
                found = true;
                break;
            }
            if (!found) {
                // Remove this active point (swap-pop).
                active_[ai] = active_.back();
                active_.pop_back();
            }
        }
    }

    std::size_t PointCount() const { return points_.size(); }
    bool PointAt(std::size_t i, float& x, float& y) const {
        if (i >= points_.size()) return false;
        x = points_[i].x; y = points_[i].y; return true;
    }

private:
    struct Pt { float x, y; };
    static float Rand(uint32_t& s) {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s % 10000u) / 10000.0f;
    }
    bool HasNear(float x, float y) const {
        int gx = static_cast<int>(x / cell_);
        int gy = static_cast<int>(y / cell_);
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                int cx = gx + dx, cy = gy + dy;
                if (cx < 0 || cy < 0 || cx >= gw_ || cy >= gh_) continue;
                int idx = grid_[static_cast<std::size_t>(cy) * gw_ + cx];
                if (idx < 0) continue;
                float px = points_[idx].x, py = points_[idx].y;
                float ddx = x - px, ddy = y - py;
                if (ddx * ddx + ddy * ddy < r_ * r_) return true;
            }
        return false;
    }
    void AddPoint(float x, float y) {
        points_.push_back({x, y});
        int gx = static_cast<int>(x / cell_);
        int gy = static_cast<int>(y / cell_);
        grid_[static_cast<std::size_t>(gy) * gw_ + gx] = static_cast<int>(points_.size() - 1);
    }
    std::vector<Pt> points_;
    std::vector<int> grid_;
    std::vector<std::size_t> active_;
    float w_=1, h_=1, r_=1, cell_=1;
    int gw_=1, gh_=1;
};

} // namespace bighero
