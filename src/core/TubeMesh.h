#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cmath>

namespace bighero {

// TubeMesh: builds a tube/cylinder mesh along the Z axis given a radius
// profile. Pure CPU-side geometry builder producing vertex/indices.
class TubeMesh {
public:
    TubeMesh() {}
    TubeMesh(std::size_t radialSegments, float radius, float length)
        : radial_(radialSegments < 3 ? 3 : radialSegments),
          radius_(radius), length_(length) {}

    void SetRadialSegments(std::size_t s) { radial_ = s < 3 ? 3 : s; }
    std::size_t RadialSegments() const { return radial_; }
    void SetRadius(float r) { radius_ = r < 0 ? 0 : r; }
    float Radius() const { return radius_; }
    void SetLength(float l) { length_ = l; }
    float Length() const { return length_; }

    // Build a cylinder (open-ended tube). Returns vertex count.
    std::size_t Generate(std::vector<float>& verts,
                         std::vector<std::uint32_t>& indices) const {
        verts.clear(); indices.clear();
        float hz = length_ * 0.5f;
        for (std::size_t i = 0; i < radial_; ++i) {
            float a = (float)i / (float)radial_ * 2.0f * 3.14159265f;
            float x = std::cos(a)*radius_, z = std::sin(a)*radius_;
            verts.push_back(x); verts.push_back(-hz); verts.push_back(z);
            verts.push_back(x); verts.push_back( hz); verts.push_back(z);
        }
        for (std::size_t i = 0; i < radial_; ++i) {
            std::uint32_t i0 = (std::uint32_t)(2*i);
            std::uint32_t i1 = (std::uint32_t)(2*((i+1)%radial_));
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i0+1);
            indices.push_back(i1); indices.push_back(i1+1); indices.push_back(i0+1);
        }
        return radial_ * 2;
    }

private:
    std::size_t radial_ = 16;
    float radius_ = 0.5f, length_ = 1.0f;
};

} // namespace bighero
