#pragma once
#include <cmath>

namespace bighero {

// Deterministic hash-based white noise in 1D/2D/3D, values in [0,1).
class Noise {
public:
    // 1D white noise: deterministic from a single integer coordinate + seed.
    static float Noise1D(int x, unsigned seed = 0) {
        unsigned h = Hash((unsigned)x, 0, 0, seed);
        return (h & 0xFFFFu) / 65535.0f;
    }

    static float Noise2D(int x, int y, unsigned seed = 0) {
        unsigned h = Hash((unsigned)x, (unsigned)y, 0, seed);
        return (h & 0xFFFFu) / 65535.0f;
    }

    static float Noise3D(int x, int y, int z, unsigned seed = 0) {
        unsigned h = Hash((unsigned)x, (unsigned)y, (unsigned)z, seed);
        return (h & 0xFFFFu) / 65535.0f;
    }

    static unsigned Hash(unsigned x, unsigned y, unsigned z, unsigned seed) {
        unsigned h = seed ^ (x * 374761393u) ^ (y * 668265263u) ^ (z * 1103515245u);
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }
};

} // namespace bighero
