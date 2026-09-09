#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace bighero {

// FbmNoise: fractional Brownian motion over an injected 2D noise function
// (single seed = repeatable). Standard-library only, self-contained.
class FbmNoise {
public:
    FbmNoise(uint32_t seed = 1337u, int octaves = 5, float persistence = 0.5f,
             float lacunarity = 2.0f)
        : seed_(seed), octaves_(octaves > 0 ? octaves : 1),
          persistence_(persistence), lacunarity_(lacunarity) {}

    float Noise(float x, float y) const {
        float total = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
        for (int o = 0; o < octaves_; ++o) {
            float n = Sample(x * freq, y * freq, seed_ + static_cast<uint32_t>(o));
            total += n * amp;
            norm += amp;
            amp *= persistence_;
            freq *= lacunarity_;
        }
        return norm > 0 ? total / norm : 0.0f;
    }

private:
    static float Sample(float x, float y, uint32_t seed) {
        // Cheap deterministic lattice value noise.
        int xi = static_cast<int>(std::floor(x));
        int yi = static_cast<int>(std::floor(y));
        float fx = x - xi, fy = y - yi;
        float sx = fx * fx * (3 - 2 * fx);
        float sy = fy * fy * (3 - 2 * fy);
        float v00 = Hash(xi, yi, seed), v10 = Hash(xi+1, yi, seed);
        float v01 = Hash(xi, yi+1, seed), v11 = Hash(xi+1, yi+1, seed);
        float a = v00 + (v10 - v00) * sx;
        float b = v01 + (v11 - v01) * sx;
        return a + (b - a) * sy;
    }
    static float Hash(int x, int y, uint32_t seed) {
        uint32_t n = static_cast<uint32_t>(x) * 374761393u +
                     static_cast<uint32_t>(y) * 668265263u + seed * 974634631u;
        n = (n ^ (n >> 13)) * 1274126177u;
        n = n ^ (n >> 16);
        return static_cast<float>(n & 0xFFFFu) / 65535.0f;
    }
    uint32_t seed_;
    int octaves_;
    float persistence_, lacunarity_;
};

} // namespace bighero
