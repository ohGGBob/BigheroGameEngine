#pragma once
// 值噪声（ValueNoise）：可复现的平滑噪声（确定性），用于地形/纹理/随机地形。
// 纯标准库、仅头文件。
//
// 商业化价值：程序化地形高度、植被分布、云层纹理等"平滑且可复现"的伪随机噪声；
// 基于哈希整数栅格点 + 双线性插值，无需外部依赖。
//
// 提供：
//   - 1D / 2D / 3D 单层噪声（返回 [0,1]）。
//   - 多倍频（FBM）叠加 noise2D 以获得更丰富的细节。
//   - 固定 seed 保证可复现。

#include <cmath>
#include <cstdint>

namespace BigHero::Core
{
class ValueNoise
{
  public:
    explicit ValueNoise(unsigned seed = 1337) : seed_(seed) {}

    float Noise1D(float x) const
    {
        int ix = (int)std::floor(x);
        float fx = x - ix;
        float v0 = Hash01(ix, 0);
        float v1 = Hash01(ix + 1, 0);
        float u = fx * fx * (3.0f - 2.0f * fx);
        return v0 + (v1 - v0) * u;
    }

    float Noise2D(float x, float y) const
    {
        int ix = (int)std::floor(x);
        int iy = (int)std::floor(y);
        float fx = x - ix;
        float fy = y - iy;
        float v00 = Hash01(ix, iy);
        float v10 = Hash01(ix + 1, iy);
        float v01 = Hash01(ix, iy + 1);
        float v11 = Hash01(ix + 1, iy + 1);
        float ux = fx * fx * (3.0f - 2.0f * fx);
        float uy = fy * fy * (3.0f - 2.0f * fy);
        float a = v00 + (v10 - v00) * ux;
        float b = v01 + (v11 - v01) * ux;
        return a + (b - a) * uy;
    }

    // FBM 多倍频叠加（octaves 层，每层频率翻倍、幅度减半）。
    float Fbm2D(float x, float y, int octaves = 4) const
    {
        float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
        for (int i = 0; i < octaves; ++i)
        {
            sum += Noise2D(x * freq, y * freq) * amp;
            norm += amp;
            amp *= 0.5f;
            freq *= 2.0f;
        }
        return sum / (norm > 0.0f ? norm : 1.0f);
    }

  private:
    // 哈希一个整型坐标对，返回 [0,1] 的伪随机值（确定性、稳定）。
    float Hash01(int x, int y) const
    {
        uint32_t h = (uint32_t)seed_;
        h ^= (uint32_t)x * 0x9E3779B9u;
        h ^= (uint32_t)y * 0x85EBCA6Bu;
        h = (h ^ (h >> 16)) * 0x85EBCA6Bu;
        h = (h ^ (h >> 13)) * 0xC2B2AE35u;
        h ^= (h >> 16);
        return (h & 0xFFFFFFu) / (float)0x1000000u;
    }

    unsigned seed_;
};
} // namespace BigHero::Core
