#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace bighero {

// VoronoiDiagram: generates a set of seed points and computes the nearest seed
// (and distance) for any query point. Standard-library only, self-contained.
class VoronoiDiagram {
public:
    VoronoiDiagram() {}
    VoronoiDiagram(uint32_t seed, std::size_t count, float width, float height)
        { Generate(seed, count, width, height); }

    void Generate(uint32_t seed, std::size_t count, float width, float height) {
        seeds_.clear();
        width_ = width > 0 ? width : 1.0f;
        height_ = height > 0 ? height : 1.0f;
        uint32_t s = seed ? seed : 1u;
        for (std::size_t i = 0; i < count; ++i) {
            s = s * 1664525u + 1013904223u;
            float px = (static_cast<float>(s % 10000u) / 10000.0f) * width_;
            s = s * 1664525u + 1013904223u;
            float py = (static_cast<float>(s % 10000u) / 10000.0f) * height_;
            seeds_.push_back({px, py});
        }
    }

    // Returns index of nearest seed, sets distSq to squared distance.
    std::size_t Nearest(float x, float y, float& distSq) const {
        if (seeds_.empty()) { distSq = -1.0f; return 0; }
        std::size_t best = 0;
        float bestD = 1e30f;
        for (std::size_t i = 0; i < seeds_.size(); ++i) {
            float dx = x - seeds_[i].x, dy = y - seeds_[i].y;
            float d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = i; }
        }
        distSq = bestD;
        return best;
    }
    // Nearest index only.
    std::size_t Nearest(float x, float y) const {
        float dummy; return Nearest(x, y, dummy);
    }

    std::size_t SeedCount() const { return seeds_.size(); }
    float Width() const { return width_; }
    float Height() const { return height_; }
    bool SeedAt(std::size_t i, float& x, float& y) const {
        if (i >= seeds_.size()) return false;
        x = seeds_[i].x; y = seeds_[i].y; return true;
    }

private:
    struct Seed { float x, y; };
    std::vector<Seed> seeds_;
    float width_=1.0f, height_=1.0f;
};

} // namespace bighero
