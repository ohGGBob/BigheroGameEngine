#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cmath>

namespace bighero {

// CapsuleMesh: builds a capsule (cylinder + two hemispherical caps) along the
// Y axis. Pure CPU-side geometry builder — vertex/indices output.
class CapsuleMesh {
public:
    CapsuleMesh() {}
    CapsuleMesh(float radius, float cylinderHeight, std::size_t segments)
        : radius_(radius < 0 ? 0 : radius),
          cylHeight_(cylinderHeight < 0 ? 0 : cylinderHeight),
          segments_(segments < 3 ? 3 : segments) {}

    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }
    void SetCylinderHeight(float h) { cylHeight_ = h < 0 ? 0 : h; }
    float CylinderHeight() const { return cylHeight_; }
    void SetSegments(std::size_t s) { segments_ = s < 3 ? 3 : s; }
    std::size_t Segments() const { return segments_; }

    // Generate capsule vertex/indices. Returns vertex count.
    std::size_t Generate(std::vector<float>& verts,
                         std::vector<std::uint32_t>& indices) const {
        verts.clear(); indices.clear();
        float hh = cylHeight_ * 0.5f;
        // ring at y=-hh (bottom cap equator), y=+hh (top cap equator),
        // plus a bottom pole/apex ring and a top pole/apex ring.
        auto ring = [&](float cy, float r) {
            for (std::size_t s = 0; s < segments_; ++s) {
                float th = (float)s / (float)segments_ * 2.0f * 3.14159265f;
                verts.push_back(std::cos(th)*r);
                verts.push_back(cy);
                verts.push_back(std::sin(th)*r);
            }
        };
        float bottomApexY = -hh - radius_;
        float topApexY = hh + radius_;
        ring(bottomApexY, 0.0f);            // bottom apex ring (degenerate)
        ring(-hh, radius_);                  // bottom equator
        ring(hh, radius_);                   // top equator
        ring(topApexY, 0.0f);                // top apex ring (degenerate)
        for (std::size_t s = 0; s < segments_; ++s) {
            std::uint32_t a0 = (std::uint32_t)(s);
            std::uint32_t a1 = (std::uint32_t)((s+1)%segments_);
            std::uint32_t b0 = (std::uint32_t)(segments_ + s);
            std::uint32_t b1 = (std::uint32_t)(segments_ + (s+1)%segments_);
            // bottom cap
            indices.push_back(a0); indices.push_back(b0); indices.push_back(a1);
            indices.push_back(a1); indices.push_back(b0); indices.push_back(b1);
            std::uint32_t c0 = (std::uint32_t)(2*segments_ + s);
            std::uint32_t c1 = (std::uint32_t)(2*segments_ + (s+1)%segments_);
            std::uint32_t d0 = (std::uint32_t)(3*segments_ + s);
            std::uint32_t d1 = (std::uint32_t)(3*segments_ + (s+1)%segments_);
            // top cap
            indices.push_back(c0); indices.push_back(c1); indices.push_back(d0);
            indices.push_back(d0); indices.push_back(c1); indices.push_back(d1);
        }
        return 4 * segments_;
    }

private:
    float radius_ = 0.5f, cylHeight_ = 1.0f;
    std::size_t segments_ = 16;
};

} // namespace bighero
