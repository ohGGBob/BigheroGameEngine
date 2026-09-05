#pragma once
// 轻量级随机数生成器（替代 <random>，避免 MSVC STL C++23 泄漏问题）
// 使用 PCG32 算法：高质量、无状态、线程不安全（单线程粒子系统足够）

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace BigHero::Core
{

class FastRng
{
  public:
    using result_type = uint32_t;

    explicit FastRng(uint64_t seed = 0x853c49e6748fea9bULL) noexcept
        : state_(seed ? seed : 0x853c49e6748fea9bULL), inc_(0xda3e39cb94b95bdbULL | 1ULL)
    {
        (void)Next(); // 混合初始状态（结果仅用于推进状态，无需返回值）
    }

    void Seed(uint64_t seed) noexcept
    {
        state_ = seed ? seed : 0x853c49e6748fea9bULL;
        (void)Next();
    }

    [[nodiscard]] uint32_t Next() noexcept
    {
        const uint64_t old_state = state_;
        state_ = old_state * 6364136223846793005ULL + inc_;
        const uint32_t xorshifted = static_cast<uint32_t>(((old_state >> 18u) ^ old_state) >> 27u);
        const uint32_t rot = static_cast<uint32_t>(old_state >> 59u);
        // 避免对无符号做一元取负（MSVC C4146）：32-rot 与 -rot 模 32 等价（&31 截断）。
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // [0, 1) 均匀浮点数
    [[nodiscard]] float NextFloat() noexcept
    {
        // 使用位操作生成 [0, 1) 浮点数：将 23 位随机位放入尾数
        const uint32_t u = Next() >> 9;
        return static_cast<float>(u) * 0x1.0p-23f; // 1/2^23
    }

    // [-1, 1) 均匀浮点数
    [[nodiscard]] float NextFloatSym() noexcept { return NextFloat() * 2.0f - 1.0f; }

    // [min, max) 均匀浮点数
    [[nodiscard]] float NextFloat(float min, float max) noexcept { return min + (max - min) * NextFloat(); }

    // 均匀球面采样
    [[nodiscard]] float NextGaussian() noexcept
    {
        // Box-Muller 变换（简化版，生成标准正态分布）
        static bool has_spare = false;
        static float spare = 0.0f;
        if (has_spare)
        {
            has_spare = false;
            return spare;
        }
        const float u1 = NextFloat();
        const float u2 = NextFloat();
        const float mag = std::sqrt(-2.0f * std::log(std::max(u1, 1e-7f)));
        const float z0 = mag * std::cos(6.2831853f * u2);
        spare = mag * std::sin(6.2831853f * u2);
        has_spare = true;
        return z0;
    }

    static constexpr uint32_t min() noexcept { return 0; }
    static constexpr uint32_t max() noexcept { return 0xFFFFFFFFu; }

  private:
    uint64_t state_;
    uint64_t inc_;
};

} // namespace BigHero::Core
