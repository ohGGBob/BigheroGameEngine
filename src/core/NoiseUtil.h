#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// NoiseUtil: small stateless PRNG/hash helpers used to derive deterministic
// noise values from integer coordinates (e.g. for chunk seeds and wobble).
// Self-contained, standard-library only.
class NoiseUtil {
public:
    // Integer hash producing a value in [0, 2^32).
    static uint32_t Hash(uint32_t x, uint32_t y, uint32_t seed) {
        uint32_t h = seed ^ (x * 0x27d4eb2du) ^ (y * 0x9e3779b9u);
        h ^= h >> 16; h *= 0x85ebca6bu;
        h ^= h >> 13; h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return h;
    }
    // Normalized hash in [0,1).
    static float Hash01(uint32_t x, uint32_t y, uint32_t seed) {
        return (Hash(x, y, seed) & 0x00FFFFFFu) / 16777216.0f;
    }
    // Simple integer LCG in [0, 2^24).
    static uint32_t LCG(uint32_t& state) {
        state = 1664525u * state + 1013904223u;
        return (state >> 8) & 0x00FFFFFFu;
    }
    // Fade curve for value-noise interpolation.
    static float Fade(float t) { return t * t * (3 - 2 * t); }
    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
    // Wrap a coordinate into [0, period).
    static int Repeat(int v, int period) {
        if (period <= 0) return 0;
        int r = v % period;
        return r < 0 ? r + period : r;
    }
};

} // namespace bighero
