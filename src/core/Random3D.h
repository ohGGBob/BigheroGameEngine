#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// 3D random point / vector generator derived from a deterministic seed.
// Produces positions inside boxes, spheres, and random unit directions.
class Random3D {
public:
    explicit Random3D(std::uint64_t seed = 0) : state_(seed) {}

    void Seed(std::uint64_t seed) { state_ = seed; }

    std::uint32_t Next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (std::uint32_t)(z ^ (z >> 31));
    }

    float NextFloat() { return (Next() >> 8) / 16777216.0f; }

    // Uniform point in an axis-aligned box [minX,maxX]x[minY,maxY]x[minZ,maxZ].
    void PointInBox(float minX, float minY, float minZ,
                    float maxX, float maxY, float maxZ,
                    float& outX, float& outY, float& outZ) {
        outX = minX + NextFloat() * (maxX - minX);
        outY = minY + NextFloat() * (maxY - minY);
        outZ = minZ + NextFloat() * (maxZ - minZ);
    }

    // Uniform point in a unit sphere centered at origin.
    void PointInSphere(float& outX, float& outY, float& outZ) {
        float u = NextFloat() * 2.0f - 1.0f;
        float phi = NextFloat() * 6.2831853f;
        float r = std::cbrt(NextFloat());
        float s = std::sqrt(1.0f - u * u);
        outX = r * s * std::cos(phi);
        outY = r * s * std::sin(phi);
        outZ = r * u;
    }

    // Uniform point on a unit sphere surface.
    void PointOnSphere(float& outX, float& outY, float& outZ) {
        float u = NextFloat() * 2.0f - 1.0f;
        float phi = NextFloat() * 6.2831853f;
        float s = std::sqrt(1.0f - u * u);
        outX = s * std::cos(phi);
        outY = s * std::sin(phi);
        outZ = u;
    }

private:
    std::uint64_t state_;
};

} // namespace bighero
