#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cmath>

namespace bighero {

// DomeMesh: builds a hemisphere/dome by sweeping latitude rings. Pure CPU-side
// geometry builder producing vertices + indices.
class DomeMesh {
public:
    DomeMesh() {}
    DomeMesh(std::size_t rings, std::size_t segments, float radius)
        : rings_(rings < 1 ? 1 : rings),
          segments_(segments < 3 ? 3 : segments),
          radius_(radius) {}

    void SetRings(std::size_t r) { rings_ = r < 1 ? 1 : r; }
    std::size_t Rings() const { return rings_; }
    void SetSegments(std::size_t s) { segments_ = s < 3 ? 3 : s; }
    std::size_t Segments() const { return segments_; }
    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }

    // Build a dome (upper hemisphere). Returns vertex count.
    std::size_t Generate(std::vector<float>& verts,
                         std::vector<std::uint32_t>& indices) const {
        verts.clear(); indices.clear();
        for (std::size_t r = 0; r <= rings_; ++r) {
            float phi = (float)r / (float)rings_ * 1.57079632f;  // 0..pi/2
            float y = std::cos(phi) * radius_;
            float ringRad = std::sin(phi) * radius_;
            for (std::size_t s = 0; s < segments_; ++s) {
                float th = (float)s / (float)segments_ * 2.0f * 3.14159265f;
                verts.push_back(std::cos(th)*ringRad);
                verts.push_back(y);
                verts.push_back(std::sin(th)*ringRad);
            }
        }
        for (std::size_t r = 0; r < rings_; ++r) {
            for (std::size_t s = 0; s < segments_; ++s) {
                std::uint32_t i0 = (std::uint32_t)(r*segments_ + s);
                std::uint32_t i1 = (std::uint32_t)(r*segments_ + (s+1)%segments_);
                std::uint32_t i2 = (std::uint32_t)((r+1)*segments_ + s);
                std::uint32_t i3 = (std::uint32_t)((r+1)*segments_ + (s+1)%segments_);
                indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
                indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
            }
        }
        return (rings_+1) * segments_;
    }

private:
    std::size_t rings_ = 8, segments_ = 16;
    float radius_ = 0.5f;
};

} // namespace bighero
