#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace bighero {

// SeededRandom: a deterministic, self-contained pseudo-random generator with a
// 64-bit state (SplitMix64-style). Provides float/int uniform and range
// helpers. Standard-library only, self-contained.
class SeededRandom {
public:
    SeededRandom() {}
    explicit SeededRandom(uint64_t seed) { Reseed(seed); }

    void Reseed(uint64_t seed) {
        state_ = seed ? seed : 0x9E3779B97F4A7C15ULL;
    }

    // Next 64-bit value (SplitMix64 mixing).
    uint64_t NextU64() {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Float in [0,1).
    float NextFloat() {
        return (float)(NextU64() >> 40) / (float)(1u << 24);
    }

    // Float in [min,max).
    float Range(float min, float max) {
        if (max < min) { float t=min; min=max; max=t; }
        return min + (max-min) * NextFloat();
    }

    int Int(int min, int maxInclusive) {
        if (maxInclusive < min) { int t=min; min=maxInclusive; maxInclusive=t; }
        uint64_t span = (uint64_t)(maxInclusive - min) + 1ULL;
        return min + (int)(NextU64() % span);
    }

    // Uniform int in [0, n-1].
    int IntBelow(int n) {
        if (n <= 0) return 0;
        return (int)(NextU64() % (uint64_t)n);
    }

    bool Bool(float prob = 0.5f) {
        return NextFloat() < prob;
    }

private:
    uint64_t state_ = 0x9E3779B97F4A7C15ULL;
};

} // namespace bighero
