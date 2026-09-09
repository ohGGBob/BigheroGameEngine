#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cmath>

namespace bighero {

// PrismGeometry: generates vertices/indices for a prism (n-gon extrusion).
// Pure CPU-side geometry builder; writes into caller-provided vectors.
class PrismGeometry {
public:
    PrismGeometry() {}
    PrismGeometry(std::size_t sides, float height)
        : sides_(sides < 3 ? 3 : sides), height_(height) {}

    void SetSides(std::size_t s) { sides_ = s < 3 ? 3 : s; }
    std::size_t Sides() const { return sides_; }
    void SetHeight(float h) { height_ = h; }
    float Height() const { return height_; }

    // Generate vertices [x,y,z]×3 and triangles into outVerts/outIndices.
    // Returns number of vertices.
    std::size_t Generate(std::vector<float>& verts,
                         std::vector<std::uint32_t>& indices) const {
        verts.clear(); indices.clear();
        std::size_t ring = sides_ * 2;
        for (std::size_t i = 0; i < sides_; ++i) {
            float a = (float)i / (float)sides_ * 2.0f * 3.14159265f;
            float x = std::cos(a), z = std::sin(a);
            verts.push_back(x); verts.push_back(-height_*0.5f); verts.push_back(z);
            verts.push_back(x); verts.push_back( height_*0.5f); verts.push_back(z);
        }
        // side quads -> 2 triangles
        for (std::size_t i = 0; i < sides_; ++i) {
            std::uint32_t i0 = (std::uint32_t)(2*i);
            std::uint32_t i1 = (std::uint32_t)(2*((i+1)%sides_));
            indices.push_back(i0); indices.push_back(i1); indices.push_back(i0+1);
            indices.push_back(i1); indices.push_back(i1+1); indices.push_back(i0+1);
        }
        return ring;
    }

private:
    std::size_t sides_ = 6;
    float height_ = 1.0f;
};

} // namespace bighero
