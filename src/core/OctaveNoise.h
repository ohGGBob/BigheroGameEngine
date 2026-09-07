#pragma once
#include <cmath>

namespace bighero {

// Layered octave noise built on a simple value-noise basis.
// Deterministic: given the same (seed,x,y) it returns the same result.
class OctaveNoise {
public:
    explicit OctaveNoise(unsigned seed = 1337) : seed_(seed) {}

    // Single octave value noise in [0,1] at lattice (x,y).
    float ValueNoise(float x, float y) const {
        int xi = (int)std::floor(x), yi = (int)std::floor(y);
        float xf = x - xi, yf = y - yi;
        float u = Fade(xf), v = Fade(yf);
        float n00 = Hash2(xi, yi);
        float n10 = Hash2(xi + 1, yi);
        float n01 = Hash2(xi, yi + 1);
        float n11 = Hash2(xi + 1, yi + 1);
        float nx0 = n00 + (n10 - n00) * u;
        float nx1 = n01 + (n11 - n01) * u;
        return nx0 + (nx1 - nx0) * v;
    }

    // Fractional Brownian motion over `octaves` layers, normalized to [0,1].
    float Fbm(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const {
        float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
        for (int o = 0; o < octaves; ++o) {
            sum += amp * ValueNoise(x * freq, y * freq);
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0 ? sum / norm : 0.0f;
    }

private:
    float Fade(float t) const { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
    float Hash2(int x, int y) const {
        unsigned h = seed_;
        h ^= (unsigned)x * 0x9E3779B1u;
        h ^= (unsigned)y * 0x85EBCA77u;
        h ^= h >> 16; h *= 0x7FEB352Du;
        h ^= h >> 15; h *= 0x846CA68Bu;
        h ^= h >> 16;
        return (h & 0xFFFFu) / (float)0xFFFFu; // [0,1]
    }
    unsigned seed_;
};

} // namespace bighero
