#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// PerlinNoise: a classic 2D/3D gradient-value Perlin noise generator with a
// permutation table and smoothstep fade. Standard-library only, self-contained.
class PerlinNoise {
public:
    explicit PerlinNoise(uint32_t seed = 1337u) { Reseed(seed); }

    void Reseed(uint32_t seed) {
        // Build a permutation table with a simple LCG shuffle.
        for (int i = 0; i < 256; ++i) p_[i] = static_cast<uint8_t>(i);
        uint32_t s = seed ? seed : 1u;
        for (int i = 255; i > 0; --i) {
            s = s * 1664525u + 1013904223u;
            int j = static_cast<int>(s % static_cast<uint32_t>(i + 1));
            uint8_t t = p_[i]; p_[i] = p_[j]; p_[j] = t;
        }
        for (int i = 0; i < 256; ++i) p_[i + 256] = p_[i];
    }

    // 2D noise in [0,1].
    float Noise(float x, float y) const {
        int X = static_cast<int>(std::floor(x)) & 255;
        int Y = static_cast<int>(std::floor(y)) & 255;
        x -= std::floor(x);
        y -= std::floor(y);
        float u = Fade(x), v = Fade(y);
        int aa = p_[p_[X] + Y], ab = p_[p_[X] + Y + 1];
        int ba = p_[p_[X + 1] + Y], bb = p_[p_[X + 1] + Y + 1];
        float x1 = Lerp(Grad(aa, x, y), Grad(ba, x - 1, y), u);
        float x2 = Lerp(Grad(ab, x, y - 1), Grad(bb, x - 1, y - 1), u);
        return 0.5f * (Lerp(x1, x2, v) + 1.0f);
    }

    // 3D noise in [0,1].
    float Noise3(float x, float y, float z) const {
        int X = static_cast<int>(std::floor(x)) & 255;
        int Y = static_cast<int>(std::floor(y)) & 255;
        int Z = static_cast<int>(std::floor(z)) & 255;
        x -= std::floor(x); y -= std::floor(y); z -= std::floor(z);
        float u = Fade(x), v = Fade(y), w = Fade(z);
        int p[512];
        for (int i = 0; i < 512; ++i) p[i] = p_[i & 255];
        int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
        int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
        float x1 = Lerp(Grad(p[AA], x, y, z),     Grad(p[BA], x-1, y, z), u);
        float x2 = Lerp(Grad(p[AB], x, y-1, z),   Grad(p[BB], x-1, y-1, z), u);
        float x3 = Lerp(Grad(p[AA+1], x, y, z-1), Grad(p[BA+1], x-1, y, z-1), u);
        float x4 = Lerp(Grad(p[AB+1], x, y-1, z-1), Grad(p[BB+1], x-1, y-1, z-1), u);
        float y1 = Lerp(x1, x2, v), y2 = Lerp(x3, x4, v);
        return 0.5f * (Lerp(y1, y2, w) + 1.0f);
    }

private:
    static float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static float Lerp(float a, float b, float t) { return a + t * (b - a); }
    static float Grad(int h, float x, float y) {
        switch (h & 3) {
            case 0: return x + y; case 1: return -x + y;
            case 2: return x - y; default: return -x - y;
        }
    }
    static float Grad(int h, float x, float y, float z) {
        switch (h & 15) {
            case 0: return x+y; case 1: return -x+y; case 2: return x-y;
            case 3: return -x-y; case 4: return x+z; case 5: return -x+z;
            case 6: return x-z; case 7: return -x-z; case 8: return y+z;
            case 9: return -y+z; case 10: return y-z; case 11: return -y-z;
            case 12: return x+y; case 13: return -y; default: return x-y;
        }
    }
    uint8_t p_[512]{};
};

} // namespace bighero
