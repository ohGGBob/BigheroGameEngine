#pragma once
#include <cstdint>
#include <random>

namespace bighero {

// Random: a seeded pseudo-random generator (xorshift128+ backed) with helpers
// for uniform ints, floats and ranges. Deterministic when seeded identically.
// Self-contained, std-lib only.
class Random {
public:
    Random() { Seed(0x9e3779b97f4a7c15ull); }
    explicit Random(uint64_t seed) { Seed(seed); }
    explicit Random(int seed) { Seed((uint64_t)seed); }

    void Seed(uint64_t seed) {
        state_[0] = seed ? seed : 0xdeadbeefcafebabeull;
        state_[1] = seed ^ 0x9e3779b97f4a7c15ull;
        if (state_[1] == 0) state_[1] = 0x123456789abcdefull;
    }

    // Uniform 64-bit unsigned.
    uint64_t NextU64() {
        uint64_t x = state_[0];
        uint64_t y = state_[1];
        x ^= x << 23;
        state_[0] = y;
        state_[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
        return state_[1] + x;
    }
    uint32_t NextU32() { return (uint32_t)(NextU64() >> 32); }
    int NextInt() { return (int)(NextU64() >> 33); }
    int NextInt(int max) { return max <= 0 ? 0 : (int)(NextU64() % (uint64_t)max); }
    int Range(int lo, int hi) { return hi <= lo ? lo : lo + NextInt(hi - lo); }

    // Uniform float in [0,1).
    float NextFloat() { return (float)(NextU64() >> 40) / (float)(1ull << 24); }
    // Uniform float in [lo,hi).
    float Range(float lo, float hi) { return lo + (hi - lo) * NextFloat(); }
    double NextDouble() { return (double)(NextU64() >> 11) / (double)(1ull << 53); }

    bool NextBool() { return (NextU64() & 1) != 0; }

private:
    uint64_t state_[2];
};

} // namespace bighero
