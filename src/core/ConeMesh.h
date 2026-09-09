#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cmath>

namespace bighero {

// ConeMesh: builds a cone (base disc + apex) along the Y axis. Pure
// CPU-side geometry builder producing vertices + indices.
class ConeMesh {
public:
    ConeMesh() {}
    ConeMesh(float radius, float height, std::size_t segments)
        : radius_(radius < 0 ? 0 : radius),
          height_(height < 0 ? 0 : height),
          segments_(segments < 3 ? 3 : segments) {}

    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }
    void SetHeight(float h) { height_ = h < 0 ? 0 : h; }
    float Height() const { return height_; }
    void SetSegments(std::size_t s) { segments_ = s < 3 ? 3 : s; }
    std::size_t Segments() const { return segments_; }

    // Build cone. Returns vertex count.
    std::size_t Generate(std::vector<float>& verts,
                         std::vector<std::uint32_t>& indices) const {
        verts.clear(); indices.clear();
        float baseY = -height_ * 0.5f;
        float apexY = height_ * 0.5f;
        for (std::size_t s = 0; s < segments_; ++s) {
            float th = (float)s / (float)segments_ * 2.0f * 3.14159265f;
            verts.push_back(std::cos(th)*radius_);
            verts.push_back(baseY);
            verts.push_back(std::sin(th)*radius_);
        }
        std::uint32_t apex = (std::uint32_t)segments_;
        verts.push_back(0.0f); verts.push_back(apexY); verts.push_back(0.0f);
        for (std::size_t s = 0; s < segments_; ++s) {
            std::uint32_t i0 = (std::uint32_t)s;
            std::uint32_t i1 = (std::uint32_t)((s+1)%segments_);
            indices.push_back(i0); indices.push_back(i1); indices.push_back(apex);
            // base cap triangles (reverse winding)
            indices.push_back(i1); indices.push_back(i0); indices.push_back(apex);
        }
        return segments_ + 1;
    }

private:
    float radius_ = 0.5f, height_ = 1.0f;
    std::size_t segments_ = 16;
};

} // namespace bighero
