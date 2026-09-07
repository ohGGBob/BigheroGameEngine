#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// Small, fast, seedable RNG (xorshift128 + PCG-style mix). Drop-in for
// deterministic procedural generation and tests.
class Random {
public:
    explicit Random(uint64_t seed = 0x9E3779B97F4A7C15ull) {
        SetSeed(seed);
    }

    void SetSeed(uint64_t seed) {
        state_[0] = seed ? seed : 0x9E3779B97F4A7C15ull;
        state_[1] = 0xBF58476D1CE4E5B9ull;
        state_[2] = 0x94D049BB133111EBull;
        state_[3] = state_[0] ^ state_[1] ^ state_[2];
        for (int i = 0; i < 16; ++i) Next();
    }

    // Raw uint64 in [0, 2^64).
    uint64_t Next() {
        uint64_t t = state_[0];
        t ^= t << 11;
        t ^= t >> 8;
        state_[0] = state_[1];
        state_[1] = state_[2];
        state_[2] = state_[3];
        state_[3] ^= t;
        state_[3] ^= state_[0] ^ (state_[1] << 19);
        return state_[3];
    }

    // Float in [0,1).
    float NextFloat() {
        return (float)(Next() >> 40) / (float)(1u << 24);
    }

    // Float in [-1,1).
    float NextFloatSym() {
        return NextFloat() * 2.0f - 1.0f;
    }

    // Uniform float in [lo, hi).
    float Range(float lo, float hi) {
        return lo + NextFloat() * (hi - lo);
    }

    // Int in [lo, hi].
    int RangeInt(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + (int)(Next() % (uint64_t)(hi - lo + 1));
    }

    bool Chance(float p) { return NextFloat() < p; }

private:
    uint64_t state_[4];
};

} // namespace bighero

namespace BigHero::Core
{
// 兼容别名：游戏代码以 BigHero::Core::FastRng 引用新核心库（bighero 命名空间）的默认随机数生成器
using ::bighero::Random;
using FastRng = ::bighero::Random;
}
