#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// TerrainGenerator: procedural heightmap generation from a noise function
// (passed in as a lambda) with configurable scale, amplitude, and octave
// layering. Returns a grid of height values. Pure stdlib.
class TerrainGenerator {
public:
    TerrainGenerator() {}
    TerrainGenerator(float scale, float amplitude)
        : scale_(scale <= 0 ? 1 : scale), amplitude_(amplitude) {}

    void SetScale(float s) { scale_ = s <= 0 ? 1 : s; }
    float Scale() const { return scale_; }
    void SetAmplitude(float a) { amplitude_ = a; }
    float Amplitude() const { return amplitude_; }
    void SetSeed(float seed) { seed_ = seed; }
    float Seed() const { return seed_; }

    using NoiseFn = float (*)(float, float, float);

    // Generate a width x height grid; invokes noise(x,y,seed) per cell.
    // Returns heights in row-major order (row = y).
    std::vector<float> Generate(int width, int height, NoiseFn noise) const {
        std::vector<float> out((std::size_t)width * height);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float nx = x * scale_;
                float ny = y * scale_;
                float h = noise ? noise(nx, ny, seed_) : 0.0f;
                out[(std::size_t)y * width + x] = h * amplitude_;
            }
        }
        return out;
    }

    // Find min/max height of a generated grid.
    static void MinMax(const std::vector<float>& grid, float& mn, float& mx) {
        mn = 0; mx = 0;
        if (grid.empty()) return;
        mn = mx = grid[0];
        for (float v : grid) { if (v < mn) mn = v; if (v > mx) mx = v; }
    }

private:
    float scale_ = 1.0f;
    float amplitude_ = 1.0f;
    float seed_ = 0.0f;
};

} // namespace bighero
