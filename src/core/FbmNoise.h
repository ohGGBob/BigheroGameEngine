#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// Fractal Brownian Motion over Perlin noise: sums several octaves with
// increasing frequency and decreasing amplitude. Self-contained (own hash +
// own per-octave Perlin), no external module dependency.
class FbmNoise {
public:
    static float Noise2D(float x, float y, int octaves, float lacunarity,
                         float gain, unsigned seed = 0) {
        float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
        for (int o = 0; o < octaves; ++o) {
            sum += Perlin(x * freq, y * freq, seed + (unsigned)o * 131) * amp;
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return sum / (norm > 1e-8f ? norm : 1.0f);
    }

private:
    static unsigned Hash(unsigned x, unsigned y, unsigned seed) {
        unsigned h = seed ^ (x * 374761393u) ^ (y * 668265263u);
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }
    static float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static float Grad(unsigned x, unsigned y, unsigned seed, float dx, float dy) {
        unsigned h = Hash(x, y, seed);
        float a = (h & 0xFFFFu) / 65535.0f * 6.2831853f;
        return std::cos(a) * dx + std::sin(a) * dy;
    }
    static float Perlin(float x, float y, unsigned seed) {
        int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
        float tx = x - x0, ty = y - y0;
        float g00 = Grad((unsigned)x0,     (unsigned)y0,     seed, tx,     ty);
        float g10 = Grad((unsigned)(x0+1),(unsigned)y0,     seed, tx - 1, ty);
        float g01 = Grad((unsigned)x0,     (unsigned)(y0+1),seed, tx,     ty - 1);
        float g11 = Grad((unsigned)(x0+1),(unsigned)(y0+1),seed, tx - 1, ty - 1);
        float sx = Fade(tx), sy = Fade(ty);
        float nx0 = g00 + (g10 - g00) * sx;
        float nx1 = g01 + (g11 - g01) * sx;
        return (nx0 + (nx1 - nx0) * sy) * 0.7f;
    }
};

} // namespace bighero
