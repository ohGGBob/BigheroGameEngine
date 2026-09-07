#pragma once
#include <cstdint>

namespace bighero {

// Deterministic hash-based PRNG (splitmix64-style) producing a uniform stream
// of 32-bit values. Stateless except for internal state; good for procedural
// generation where reproducibility matters.
class HashRandom {
public:
    explicit HashRandom(std::uint64_t seed = 0) : state_(seed) {}

    void Seed(std::uint64_t seed) { state_ = seed; }

    std::uint32_t Next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (std::uint32_t)(z ^ (z >> 31));
    }

    // Float in [0,1).
    float NextFloat() { return (Next() >> 8) / 16777216.0f; }

    // Integer in [min, max] inclusive.
    int NextInt(int min, int max) {
        if (max <= min) return min;
        std::uint64_t range = (std::uint64_t)(max - min) + 1;
        return min + (int)(Next() % range);
    }

    bool NextBool() { return (Next() & 1u) != 0; }

    std::uint64_t State() const { return state_; }

private:
    std::uint64_t state_;
};

} // namespace bighero
