#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace bighero {

// TerrainErosion: a simple hydraulic-erosion relaxation over a heightfield.
// Drops sediment where the local gradient is low and erodes where it is high.
// Standard-library only, self-contained. Operates on a flat float grid.
class TerrainErosion {
public:
    TerrainErosion() {}
    TerrainErosion(std::size_t w, std::size_t h, float lambda = 0.1f,
                   float gravity = 9.8f)
        : lambda_(lambda), gravity_(gravity) { Resize(w, h); }

    void Resize(std::size_t w, std::size_t h) {
        w_ = w < 3 ? 3 : w; h_ = h < 3 ? 3 : h;
        height_.assign(w_ * h_, 0.0f);
    }
    std::size_t Width() const { return w_; }
    std::size_t Height() const { return h_; }

    bool Set(std::size_t x, std::size_t y, float h) {
        if (x >= w_ || y >= h_) return false;
        height_[y * w_ + x] = h;
        return true;
    }
    bool Get(std::size_t x, std::size_t y, float& h) const {
        if (x >= w_ || y >= h_) return false;
        h = height_[y * w_ + x];
        return true;
    }

    void SetLambda(float l) { lambda_ = l; }
    void SetGravity(float g) { gravity_ = g < 1e-3f ? 1e-3f : g; }

    // One erosion step: moves material downhill proportional to gradient and
    // lambda. In-place explicit relaxation.
    void Step() {
        std::vector<float> next(height_.size());
        for (std::size_t y = 0; y < h_; ++y)
            for (std::size_t x = 0; x < w_; ++x) {
                float z = height_[y * w_ + x];
                float total = 0.0f, count = 0.0f;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = static_cast<int>(x) + dx, ny = static_cast<int>(y) + dy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(w_) || ny >= static_cast<int>(h_)) continue;
                        total += height_[ny * w_ + nx]; ++count;
                    }
                float avg = count > 0 ? total / count : z;
                next[y * w_ + x] = z + lambda_ * (avg - z);
            }
        height_.swap(next);
    }

private:
    std::size_t w_=3, h_=3;
    float lambda_=0.1f, gravity_=9.8f;
    std::vector<float> height_;
};

} // namespace bighero
