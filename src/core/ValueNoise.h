#pragma once
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace bighero {

// ValueNoise: a fast lattice-value noise (random values on the grid,
// interpolated). Standard-library only, self-contained.
class ValueNoise {
public:
    explicit ValueNoise(uint32_t seed = 1337u) { Reseed(seed); }

    void Reseed(uint32_t seed) {
        for (int i = 0; i < 256; ++i) hash_[i] = static_cast<uint8_t>(i);
        uint32_t s = seed ? seed : 1u;
        for (int i = 255; i > 0; --i) {
            s = s * 1103515245u + 12345u;
            int j = static_cast<int>(s % static_cast<uint32_t>(i + 1));
            uint8_t t = hash_[i]; hash_[i] = hash_[j]; hash_[j] = t;
        }
    }

    float Noise(float x, float y) const {
        int xi = static_cast<int>(std::floor(x));
        int yi = static_cast<int>(std::floor(y));
        float fx = x - xi, fy = y - yi;
        float sx = Smooth(fx), sy = Smooth(fy);
        float v00 = Rand(xi, yi), v10 = Rand(xi+1, yi);
        float v01 = Rand(xi, yi+1), v11 = Rand(xi+1, yi+1);
        float a = v00 + (v10 - v00) * sx;
        float b = v01 + (v11 - v01) * sx;
        return a + (b - a) * sy;
    }

private:
    static float Smooth(float t) { return t * t * (3 - 2 * t); }
    float Rand(int x, int y) const {
        uint32_t n = static_cast<uint32_t>(x) * 374761393u +
                     static_cast<uint32_t>(y) * 668265263u +
                     static_cast<uint32_t>(hash_[(x + y) & 255]) + 0x9E3779B9u;
        n = (n ^ (n >> 13)) * 1274126177u;
        n = n ^ (n >> 16);
        return static_cast<float>(n & 0xFFFFu) / 65535.0f;
    }
    uint8_t hash_[256]{};
};

} // namespace bighero
