#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace bighero {

// WhiteNoiseTexture: fills a 2D lattice with deterministic pseudo-random
// values, producing a white-noise (uncorrelated) texture. Standard-library
// only, self-contained.
class WhiteNoiseTexture {
public:
    WhiteNoiseTexture() : w_(1), h_(1), scale_(1.0f), data_(1, 0.0f) {}
    WhiteNoiseTexture(std::size_t w, std::size_t h, uint32_t seed)
        { Generate(w, h, seed); }

    void Generate(std::size_t w, std::size_t h, uint32_t seed) {
        w_ = w < 1 ? 1 : w; h_ = h < 1 ? 1 : h;
        data_.assign(w_ * h_, 0.0f);
        uint32_t s = seed ? seed : 1u;
        for (auto& v : data_) {
            s = s * 1664525u + 1013904223u;
            v = static_cast<float>(s % 10000u) / 10000.0f;
        }
    }

    std::size_t Width() const { return w_; }
    std::size_t Height() const { return h_; }
    bool Value(std::size_t x, std::size_t y, float& v) const {
        if (x >= w_ || y >= h_) return false;
        v = data_[y * w_ + x];
        return true;
    }

private:
    std::size_t w_=1, h_=1;
    float scale_;
    std::vector<float> data_;
};

} // namespace bighero
