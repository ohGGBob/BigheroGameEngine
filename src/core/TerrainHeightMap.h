#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace bighero {

// Heightfield / terrain height map stored as a regular grid of floats.
// Provides sample/nearest height queries and simple value-noise-driven fill.
class TerrainHeightMap {
public:
    TerrainHeightMap() : width_(0), height_(0) {}

    TerrainHeightMap(int w, int h, float initVal = 0.0f) : width_(w), height_(h) {
        data_.assign((std::size_t)w * h, initVal);
    }

    void Resize(int w, int h, float initVal = 0.0f) {
        width_ = w; height_ = h;
        data_.assign((std::size_t)w * h, initVal);
    }

    int Width() const { return width_; }
    int Height() const { return height_; }

    float At(int x, int y) const {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0.0f;
        return data_[(std::size_t)y * width_ + x];
    }

    void Set(int x, int y, float v) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
        data_[(std::size_t)y * width_ + x] = v;
    }

    // Bilinear interpolation at continuous (fx, fy) in [0, width-1] x [0, height-1].
    float Sample(float fx, float fy) const {
        if (width_ < 2 || height_ < 2) return (width_ && height_) ? At(0, 0) : 0.0f;
        fx = std::max(0.0f, std::min((float)(width_ - 1), fx));
        fy = std::max(0.0f, std::min((float)(height_ - 1), fy));
        int x0 = (int)fx, y0 = (int)fy;
        int x1 = std::min(x0 + 1, width_ - 1), y1 = std::min(y0 + 1, height_ - 1);
        float tx = fx - x0, ty = fy - y0;
        float v00 = At(x0, y0), v10 = At(x1, y0);
        float v01 = At(x0, y1), v11 = At(x1, y1);
        float a = v00 * (1 - tx) + v10 * tx;
        float b = v01 * (1 - tx) + v11 * tx;
        return a * (1 - ty) + b * ty;
    }

    float MinVal() const { return data_.empty() ? 0.0f : *std::min_element(data_.begin(), data_.end()); }
    float MaxVal() const { return data_.empty() ? 0.0f : *std::max_element(data_.begin(), data_.end()); }

    // Normalize heights into [0, 1] range based on current min/max.
    void Normalize() {
        if (data_.empty()) return;
        float mn = MinVal(), mx = MaxVal();
        float range = mx - mn;
        if (range < 1e-8f) { std::fill(data_.begin(), data_.end(), 0.0f); return; }
        for (float& v : data_) v = (v - mn) / range;
    }

private:
    int width_, height_;
    std::vector<float> data_;
};

} // namespace bighero
