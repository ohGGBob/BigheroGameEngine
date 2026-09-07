#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// Self-contained 2D/3D simplex noise (seeded, deterministic).
class SimplexNoise {
public:
    explicit SimplexNoise(uint32_t seed = 12345) : seed_(seed) {}

    // Returns value in roughly [-1,1].
    float Noise2D(float x, float y) const {
        const float F2 = 0.3660254037844386f; // 0.5*(sqrt(3)-1)
        const float G2 = 0.2113248654051871f; // (3-sqrt(3))/6
        const float grad[8][2] = {{1,1},{-1,1},{1,-1},{-1,-1},{1,0},{-1,0},{0,1},{0,-1}};
        float s = (x + y) * F2;
        int i = FastFloor(x + s), j = FastFloor(y + s);
        float t = (i + j) * G2;
        float X0 = i - t, Y0 = j - t;
        float x0 = x - X0, y0 = y - Y0;
        int i1, j1;
        if (x0 > y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }
        float x1 = x0 - i1 + G2, y1 = y0 - j1 + G2;
        float x2 = x0 - 1.0f + 2.0f * G2, y2 = y0 - 1.0f + 2.0f * G2;
        int ii = i & 255, jj = j & 255;
        float n0 = 0, n1 = 0, n2 = 0;
        float t0 = 0.5f - x0 * x0 - y0 * y0;
        if (t0 > 0) { t0 *= t0; int gi = Hash(ii, jj) & 7; n0 = t0 * t0 * (grad[gi][0] * x0 + grad[gi][1] * y0); }
        float t1 = 0.5f - x1 * x1 - y1 * y1;
        if (t1 > 0) { t1 *= t1; int gi = Hash(ii + i1, jj + j1) & 7; n1 = t1 * t1 * (grad[gi][0] * x1 + grad[gi][1] * y1); }
        float t2 = 0.5f - x2 * x2 - y2 * y2;
        if (t2 > 0) { t2 *= t2; int gi = Hash(ii + 1, jj + 1) & 7; n2 = t2 * t2 * (grad[gi][0] * x2 + grad[gi][1] * y2); }
        return 70.0f * (n0 + n1 + n2);
    }

    // Returns value in roughly [-1,1].
    float Noise3D(float x, float y, float z) const {
        const float F3 = 0.3333333333333333f;
        const float G3 = 0.1666666666666666f;
        const float grad[12][3] = {
            {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
            {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
            {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
        float s = (x + y + z) * F3;
        int i = FastFloor(x + s), j = FastFloor(y + s), k = FastFloor(z + s);
        float t = (i + j + k) * G3;
        float X0 = i - t, Y0 = j - t, Z0 = k - t;
        float x0 = x - X0, y0 = y - Y0, z0 = z - Z0;
        int i1,j1,k1,i2,j2,k2;
        if (x0 >= y0) {
            if (y0 >= z0)      { i1=1;j1=0;k1=0; i2=1;j2=1;k2=0; }
            else if (x0 >= z0) { i1=1;j1=0;k1=0; i2=1;j2=0;k2=1; }
            else               { i1=0;j1=0;k1=1; i2=1;j2=0;k2=1; }
        } else {
            if (y0 < z0)       { i1=0;j1=0;k1=1; i2=0;j2=1;k2=1; }
            else if (x0 < z0)  { i1=0;j1=1;k1=0; i2=0;j2=1;k2=1; }
            else               { i1=0;j1=1;k1=0; i2=1;j2=1;k2=0; }
        }
        float x1=x0-i1+G3, y1=y0-j1+G3, z1=z0-k1+G3;
        float x2=x0-i2+2*G3, y2=y0-j2+2*G3, z2=z0-k2+2*G3;
        float x3=x0-1+3*G3,  y3=y0-1+3*G3,  z3=z0-1+3*G3;
        int ii=i&255, jj=j&255, kk=k&255;
        float n0=0,n1=0,n2=0,n3=0;
        float t0=0.6f-x0*x0-y0*y0-z0*z0;
        if (t0>0){t0*=t0;int gi=Hash3(ii,jj,kk)%12;n0=t0*t0*(grad[gi][0]*x0+grad[gi][1]*y0+grad[gi][2]*z0);}
        float t1=0.6f-x1*x1-y1*y1-z1*z1;
        if (t1>0){t1*=t1;int gi=Hash3(ii+i1,jj+j1,kk+k1)%12;n1=t1*t1*(grad[gi][0]*x1+grad[gi][1]*y1+grad[gi][2]*z1);}
        float t2=0.6f-x2*x2-y2*y2-z2*z2;
        if (t2>0){t2*=t2;int gi=Hash3(ii+i2,jj+j2,kk+k2)%12;n2=t2*t2*(grad[gi][0]*x2+grad[gi][1]*y2+grad[gi][2]*z2);}
        float t3=0.6f-x3*x3-y3*y3-z3*z3;
        if (t3>0){t3*=t3;int gi=Hash3(ii+1,jj+1,kk+1)%12;n3=t3*t3*(grad[gi][0]*x3+grad[gi][1]*y3+grad[gi][2]*z3);}
        return 32.0f * (n0 + n1 + n2 + n3);
    }

    // Fractional Brownian motion (octave accumulation).
    float Fbm2D(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const {
        float amp = 1.0f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
        for (int o = 0; o < octaves; ++o) {
            sum += amp * Noise2D(x * freq, y * freq);
            norm += amp;
            amp *= gain; freq *= lacunarity;
        }
        return sum / norm;
    }

private:
    static int FastFloor(float v) { int i = (int)v; return (v < i) ? i - 1 : i; }
    int Hash(int x, int y) const {
        uint32_t h = seed_;
        h ^= (uint32_t)x * 0x9E3779B1u;
        h ^= (uint32_t)y * 0x85EBCA77u;
        h ^= h >> 16; h *= 0x7FEB352Du;
        h ^= h >> 15; h *= 0x846CA68Bu;
        h ^= h >> 16;
        return (int)(h & 0xFFFFFFFFu);
    }
    int Hash3(int x, int y, int z) const {
        uint32_t h = seed_;
        h ^= (uint32_t)x * 0x9E3779B1u;
        h ^= (uint32_t)y * 0x85EBCA77u;
        h ^= (uint32_t)z * 0xC2B2AE3Du;
        h ^= h >> 16; h *= 0x7FEB352Du;
        h ^= h >> 15; h *= 0x846CA68Bu;
        h ^= h >> 16;
        return (int)(h & 0xFFFFFFFFu);
    }
    uint32_t seed_;
};

} // namespace bighero
