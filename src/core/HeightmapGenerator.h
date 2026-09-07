#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace bighero {

// Procedurally generates a height map grid using deterministic value-noise
// (smooth interpolation of a small random lattice) with configurable octaves.
class HeightmapGenerator {
public:
    // Generate an 'octaves'-octave fractal value-noise height field.
    static void Generate(int width, int height, int seed,
                         int octaves, float persistence,
                         float baseFrequency,
                         std::vector<float>& outHeights) {
        outHeights.assign((std::size_t)width * height, 0.0f);
        if (width < 1 || height < 1) return;

        float amp = 1.0f;
        float freq = baseFrequency;
        float total = 0.0f;
        for (int o = 0; o < octaves; ++o) {
            std::vector<float> latt;
            BuildLattice(seed + o * 131, width, height, freq, latt);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    float v = LatticeAt(latt, width, height, freq, (float)x, (float)y);
                    outHeights[(std::size_t)y * width + x] += v * amp;
                }
            }
            total += amp;
            amp *= persistence;
            freq *= 2.0f;
        }
        if (total > 1e-8f) {
            for (float& v : outHeights) v /= total;
        }
    }

    // Deterministic PRNG.
    static std::uint32_t Hash(std::uint32_t x, std::uint32_t y, std::uint32_t seed) {
        std::uint32_t h = seed ^ (x * 374761393u) ^ (y * 668265263u);
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

private:
    // Builds a lattice large enough to cover grid coords [0,w) x [0,h) at 'freq'.
    static void BuildLattice(std::uint32_t seed, int w, int h, float freq,
                             std::vector<float>& latt) {
        int lw = (int)std::ceil((w - 1) * freq) + 2;   // need index up to floor((w-1)*freq)+1
        int lh = (int)std::ceil((h - 1) * freq) + 2;
        if (lw < 2) lw = 2;
        if (lh < 2) lh = 2;
        latt.resize((std::size_t)lw * lh);
        for (int y = 0; y < lh; ++y)
            for (int x = 0; x < lw; ++x)
                latt[(std::size_t)y * lw + x] = (Hash((std::uint32_t)x, (std::uint32_t)y, seed) & 0xFFFF) / 65535.0f;
    }

    static int LatticeStride(int w, float freq) { return (int)std::ceil((w - 1) * freq) + 2; }

    static float LatticeAt(const std::vector<float>& latt, int w, int h, float freq,
                           float fx, float fy) {
        float gx = fx * freq, gy = fy * freq;
        int x0 = (int)std::floor(gx), y0 = (int)std::floor(gy);
        int x1 = x0 + 1, y1 = y0 + 1;
        float tx = gx - x0, ty = gy - y0;
        tx = Smooth(tx); ty = Smooth(ty);
        int lw = LatticeStride(w, freq);
        float v00 = latt[(std::size_t)y0 * lw + x0];
        float v10 = latt[(std::size_t)y0 * lw + x1];
        float v01 = latt[(std::size_t)y1 * lw + x0];
        float v11 = latt[(std::size_t)y1 * lw + x1];
        float a = v00 * (1 - tx) + v10 * tx;
        float b = v01 * (1 - tx) + v11 * tx;
        return a * (1 - ty) + b * ty;
    }

    static float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }
};

} // namespace bighero
