#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// 2D random point / vector generator derived from a deterministic seed.
// Produces positions inside rectangles, circles, and random unit vectors.
class Random2D {
public:
    explicit Random2D(std::uint64_t seed = 0) : state_(seed) {}

    void Seed(std::uint64_t seed) { state_ = seed; }

    std::uint32_t Next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (std::uint32_t)(z ^ (z >> 31));
    }

    float NextFloat() { return (Next() >> 8) / 16777216.0f; }

    // Uniform point in axis-aligned rectangle [minX,maxX] x [minY,maxY].
    void PointInRect(float minX, float minY, float maxX, float maxY,
                     float& outX, float& outY) {
        outX = minX + NextFloat() * (maxX - minX);
        outY = minY + NextFloat() * (maxY - minY);
    }

    // Uniform point in a unit disc centered at origin.
    void PointInDisc(float& outX, float& outY) {
        float r = std::sqrt(NextFloat());
        float a = NextFloat() * 6.2831853f;
        outX = r * std::cos(a);
        outY = r * std::sin(a);
    }

    // Uniform point on a unit circle.
    void PointOnCircle(float& outX, float& outY) {
        float a = NextFloat() * 6.2831853f;
        outX = std::cos(a);
        outY = std::sin(a);
    }

private:
    std::uint64_t state_;
};

} // namespace bighero
