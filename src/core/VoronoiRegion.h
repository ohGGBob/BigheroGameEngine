#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>

namespace bighero {

// VoronoiRegion: computes Voronoi regions for a set of seed points by nearest
// neighbor assignment. Given a set of seed points, classify an arbitrary point
// into the nearest seed's region.
// Self-contained, std-lib only.
class VoronoiRegion {
public:
    struct Seed { float x, y; };

    VoronoiRegion() = default;
    void Clear() { seeds_.clear(); }
    size_t SeedCount() const { return seeds_.size(); }
    void AddSeed(float x, float y) { seeds_.push_back({x, y}); }
    const Seed& SeedAt(size_t i) const { return seeds_[i]; }

    // Index of the nearest seed to (px,py); returns -1 if no seeds.
    int NearestSeed(float px, float py) const {
        if (seeds_.empty()) return -1;
        int best = 0;
        float bestD = SquaredDist(px, py, 0);
        for (size_t i = 1; i < seeds_.size(); ++i) {
            float d = SquaredDist(px, py, i);
            if (d < bestD) { bestD = d; best = (int)i; }
        }
        return best;
    }

    float NearestDistance(float px, float py) const {
        if (seeds_.empty()) return 0;
        int best = NearestSeed(px, py);
        // Recompute exact squared distance (caller may want sqrt).
        float dx = px - seeds_[best].x, dy = py - seeds_[best].y;
        return std::sqrt(dx*dx + dy*dy);
    }

    // Assign each point to a region: returns region index for each point.
    void Assign(const float* points, size_t pointCount, std::vector<int>& region) const {
        region.resize(pointCount);
        for (size_t i = 0; i < pointCount; ++i) {
            region[i] = NearestSeed(points[i*2], points[i*2+1]);
        }
    }

private:
    float SquaredDist(float px, float py, size_t i) const {
        float dx = px - seeds_[i].x, dy = py - seeds_[i].y;
        return dx*dx + dy*dy;
    }
    std::vector<Seed> seeds_;
};

} // namespace bighero
