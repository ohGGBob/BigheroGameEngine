#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// SimplexNoise: a 2D/3D simplex noise generator. Standard-library only,
// self-contained.
class SimplexNoise {
public:
    explicit SimplexNoise(uint32_t seed = 1337u) { Reseed(seed); }

    void Reseed(uint32_t seed) {
        for (int i = 0; i < 256; ++i) perm_[i] = static_cast<uint8_t>(i);
        uint32_t s = seed ? seed : 1u;
        for (int i = 255; i > 0; --i) {
            s = s * 1664525u + 1013904223u;
            int j = static_cast<int>(s % static_cast<uint32_t>(i + 1));
            uint8_t t = perm_[i]; perm_[i] = perm_[j]; perm_[j] = t;
        }
        for (int i = 0; i < 256; ++i) perm_[i + 256] = perm_[i];
    }

    // 2D simplex noise in rough [-1,1] (typically ~[-0.6,0.6]).
    float Noise(float xin, float yin) const {
        float n0=0, n1=0, n2=0;
        const float F2 = 0.5f * (std::sqrt(3.0f) - 1.0f);
        const float G2 = (3.0f - std::sqrt(3.0f)) / 6.0f;
        float s = (xin + yin) * F2;
        int i = static_cast<int>(std::floor(xin + s));
        int j = static_cast<int>(std::floor(yin + s));
        float t = (i + j) * G2;
        float X0 = i - t, Y0 = j - t;
        float x0 = xin - X0, y0 = yin - Y0;
        int i1, j1;
        if (x0 > y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }
        float x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
        float x2 = x0 - 1 + 2*G2, y2 = y0 - 1 + 2*G2;
        int ii = i & 255, jj = j & 255;
        int gi0 = perm_[ii + perm_[jj]] % 12;
        int gi1 = perm_[ii + i1 + perm_[jj + j1]] % 12;
        int gi2 = perm_[ii + 1 + perm_[jj + 1]] % 12;
        float t0 = 0.5f - x0*x0 - y0*y0;
        if (t0 < 0) n0 = 0; else { t0 *= t0; n0 = t0*t0*Dot2(gi0, x0, y0); }
        float t1 = 0.5f - x1*x1 - y1*y1;
        if (t1 < 0) n1 = 0; else { t1 *= t1; n1 = t1*t1*Dot2(gi1, x1, y1); }
        float t2 = 0.5f - x2*x2 - y2*y2;
        if (t2 < 0) n2 = 0; else { t2 *= t2; n2 = t2*t2*Dot2(gi2, x2, y2); }
        return 70.0f * (n0 + n1 + n2);
    }

private:
    static float Dot2(int g, float x, float y) {
        switch (g & 7) {
            case 0: return x+y; case 1: return -x+y; case 2: return x-y;
            case 3: return -x-y; case 4: return x; case 5: return -x;
            case 6: return y; default: return -y;
        }
    }
    uint8_t perm_[512]{};
};

} // namespace bighero
