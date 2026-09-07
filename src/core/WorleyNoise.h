#pragma once
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace bighero {

// Worley (cellular) noise: partition space into feature points and measure
// distance to the nearest feature. Self-contained with an internal hash.
class WorleyNoise {
public:
    // F1 (nearest) distance-based Worley noise in [0,1].
    static float Noise2D(float x, float y, unsigned seed = 0) {
        int cx = (int)std::floor(x), cy = (int)std::floor(y);
        float best = 1e30f;
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                int nx = cx + ox, ny = cy + oy;
                float px = (float)nx + FeatureX((unsigned)nx, (unsigned)ny, seed);
                float py = (float)ny + FeatureY((unsigned)nx, (unsigned)ny, seed);
                float dx = px - x, dy = py - y;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d < best) best = d;
            }
        }
        return std::min(1.0f, best * 0.5f);
    }

    // F2 (second nearest) minus F1, useful for border/crease effects.
    static float Edge2D(float x, float y, unsigned seed = 0) {
        int cx = (int)std::floor(x), cy = (int)std::floor(y);
        float f1 = 1e30f, f2 = 1e30f;
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                int nx = cx + ox, ny = cy + oy;
                float px = (float)nx + FeatureX((unsigned)nx, (unsigned)ny, seed);
                float py = (float)ny + FeatureY((unsigned)nx, (unsigned)ny, seed);
                float dx = px - x, dy = py - y;
                float d = std::sqrt(dx * dx + dy * dy);
                if (d < f1) { f2 = f1; f1 = d; }
                else if (d < f2) { f2 = d; }
            }
        }
        return std::min(1.0f, (f2 - f1) * 0.5f);
    }

private:
    static unsigned Hash(unsigned x, unsigned y, unsigned seed) {
        unsigned h = seed ^ (x * 374761393u) ^ (y * 668265263u);
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }
    static float FeatureX(unsigned x, unsigned y, unsigned seed) {
        return (Hash(x, y, seed) & 0xFFFFu) / 65535.0f - 0.5f;
    }
    static float FeatureY(unsigned x, unsigned y, unsigned seed) {
        return ((Hash(x, y, seed) >> 16) & 0xFFFFu) / 65535.0f - 0.5f;
    }
};

} // namespace bighero
