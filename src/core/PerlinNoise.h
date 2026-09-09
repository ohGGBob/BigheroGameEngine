#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// Classic 2D/3D Perlin gradient noise with smooth interpolation.
// Self-contained: each lattice gradient is derived from an internal hash.
class PerlinNoise {
public:
    // 2D Perlin noise in [-1,1].
    static float Noise2D(float x, float y, unsigned seed = 0) {
        int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
        float tx = x - x0, ty = y - y0;

        float g00 = Grad2D((unsigned)x0,     (unsigned)y0,     seed, tx,     ty);
        float g10 = Grad2D((unsigned)(x0+1), (unsigned)y0,     seed, tx - 1, ty);
        float g01 = Grad2D((unsigned)x0,     (unsigned)(y0+1), seed, tx,     ty - 1);
        float g11 = Grad2D((unsigned)(x0+1), (unsigned)(y0+1), seed, tx - 1, ty - 1);

        float sx = Fade(tx), sy = Fade(ty);
        float nx0 = g00 + (g10 - g00) * sx;
        float nx1 = g01 + (g11 - g01) * sx;
        return (nx0 + (nx1 - nx0) * sy) * 0.7f;
    }

    // 3D Perlin noise in [-1,1].
    static float Noise3D(float x, float y, float z, unsigned seed = 0) {
        int x0 = (int)std::floor(x), y0 = (int)std::floor(y), z0 = (int)std::floor(z);
        float tx = x - x0, ty = y - y0, tz = z - z0;

        float c000 = Grad3D((unsigned)x0,     (unsigned)y0,     (unsigned)z0,     seed, tx,       ty,       tz);
        float c100 = Grad3D((unsigned)(x0+1), (unsigned)y0,     (unsigned)z0,     seed, tx - 1,   ty,       tz);
        float c010 = Grad3D((unsigned)x0,     (unsigned)(y0+1), (unsigned)z0,     seed, tx,       ty - 1,   tz);
        float c110 = Grad3D((unsigned)(x0+1), (unsigned)(y0+1), (unsigned)z0,     seed, tx - 1,   ty - 1,   tz);
        float c001 = Grad3D((unsigned)x0,     (unsigned)y0,     (unsigned)(z0+1), seed, tx,       ty,       tz - 1);
        float c101 = Grad3D((unsigned)(x0+1), (unsigned)y0,     (unsigned)(z0+1), seed, tx - 1,   ty,       tz - 1);
        float c011 = Grad3D((unsigned)x0,     (unsigned)(y0+1), (unsigned)(z0+1), seed, tx,       ty - 1,   tz - 1);
        float c111 = Grad3D((unsigned)(x0+1), (unsigned)(y0+1), (unsigned)(z0+1), seed, tx - 1,   ty - 1,   tz - 1);

        float sx = Fade(tx), sy = Fade(ty), sz = Fade(tz);
        float x00 = c000 + (c100 - c000) * sx;
        float x10 = c010 + (c110 - c010) * sx;
        float x01 = c001 + (c101 - c001) * sx;
        float x11 = c011 + (c111 - c011) * sx;
        float y0_ = x00 + (x10 - x00) * sy;
        float y1_ = x01 + (x11 - x01) * sy;
        return (y0_ + (y1_ - y0_) * sz) * 0.7f;
    }

private:
    static float Fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }

    static unsigned Hash(unsigned x, unsigned y, unsigned z, unsigned seed) {
        unsigned h = seed ^ (x * 374761393u) ^ (y * 668265263u) ^ (z * 1103515245u);
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    static float Grad2D(unsigned x, unsigned y, unsigned seed, float dx, float dy) {
        unsigned h = Hash(x, y, 0, seed);
        float angle = (h & 0xFFFFu) / 65535.0f * 6.2831853f;
        return std::cos(angle) * dx + std::sin(angle) * dy;
    }

    static float Grad3D(unsigned x, unsigned y, unsigned z, unsigned seed,
                        float dx, float dy, float dz) {
        unsigned h = Hash(x, y, z, seed);
        float ox = ((h & 0xFFu) / 255.0f) * 2.0f - 1.0f;
        float oy = (((h >> 8) & 0xFFu) / 255.0f) * 2.0f - 1.0f;
        float oz = (((h >> 16) & 0xFFu) / 255.0f) * 2.0f - 1.0f;
        return ox * dx + oy * dy + oz * dz;
    }
};

} // namespace bighero
