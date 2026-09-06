#pragma once
// 轻量级随机数生成器（替代 <random>，避免 MSVC STL C++23 泄漏问题）
// 使用 PCG32 算法：高质量、无状态、线程不安全（单线程粒子系统足够）
//
// 商业化增强：
//   - NextUInt(bound)：有界无符号整数 [0, bound)，用拒绝采样消除模偏差（modulo bias）。
//   - NextInt(min, max)：有界整数 [min, max]（包含两端），内部经有符号偏移到无符号域。
//   - Shuffle(begin, end)：Fisher-Yates 均匀洗牌，供随机化实体/任务/敌人刷新顺序使用，
//     避免依赖 <random>（MSVC C++23 头文件在部分环境下触发命名空间泄漏告警）。

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

    // [min, max] 均匀整数（包含两端），处理 min>max 时自动交换
    [[nodiscard]] int NextInt(int min, int max) noexcept
    {
        if (min > max)
            std::swap(min, max);
        const uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(max) - static_cast<int64_t>(min)) + 1ULL;
        return static_cast<int>(static_cast<int64_t>(min) + static_cast<int64_t>(NextUInt32(span)));
    }

    // 有界无符号整数 [0, bound)。bound==0 视为 0（返回 0）。拒绝采样消除模偏差。
    [[nodiscard]] uint32_t NextUInt(uint32_t bound) noexcept
    {
        return bound == 0 ? 0u : NextUInt32(bound);
    }

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

    // Fisher-Yates 均匀洗牌 [first, last)。供随机化实体/任务/敌人刷新顺序使用。
    // 兼容任何随机访问迭代器（std::vector / std::array / 原生数组）。
    template<typename RandomIt>
    void Shuffle(RandomIt first, RandomIt last) noexcept
    {
        for (RandomIt it = last - 1; it != first; --it)
        {
            // 在 [0, it-first] 内均匀取 j 并交换，避免模偏差。
            const auto dist = static_cast<uint32_t>(it - first) + 1u;
            const uint32_t offset = NextUInt(static_cast<uint32_t>(dist));
            RandomIt pick = first + static_cast<decltype(it - first)>(offset);
            std::iter_swap(it, pick);
        }
    }

    static constexpr uint32_t min() noexcept { return 0; }
    static constexpr uint32_t max() noexcept { return 0xFFFFFFFFu; }

  private:
    // 在 [0, bound) 内生成无符号整数，用拒绝采样消除模偏差。
    // bound 为 0 时行为未定义（调用方保证非 0）。
    [[nodiscard]] uint32_t NextUInt32(uint64_t bound) noexcept
    {
        const uint64_t threshold = (0x100000000ULL - bound) % bound; // 需要拒绝的余数区间
        for (;;)
        {
            const uint32_t r = Next();
            if (r >= threshold)
                return static_cast<uint32_t>(r % bound);
        }
    }

    uint64_t state_;
    uint64_t inc_;
};

} // namespace BigHero::Core
