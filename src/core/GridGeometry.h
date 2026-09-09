#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bighero {

// GridGeometry: builds a flat grid in the XZ plane with a given resolution
// and size. Pure CPU-side geometry builder producing vertices + indices.
class GridGeometry {
public:
    GridGeometry() {}
    GridGeometry(std::size_t cellsX, std::size_t cellsZ, float size)
        : cellsX_(cellsX < 1 ? 1 : cellsX),
          cellsZ_(cellsZ < 1 ? 1 : cellsZ),
          size_(size) {}

    void SetCells(std::size_t x, std::size_t z) {
        cellsX_ = x < 1 ? 1 : x; cellsZ_ = z < 1 ? 1 : z;
    }
    std::size_t CellsX() const { return cellsX_; }
    std::size_t CellsZ() const { return cellsZ_; }
    void SetSize(float s) { size_ = s; }
    float Size() const { return size_; }

    // Build grid. Returns vertex count.
    std::size_t Generate(std::vector<float>& verts,
                         std::vector<std::uint32_t>& indices) const {
        verts.clear(); indices.clear();
        float half = size_ * 0.5f;
        for (std::size_t z = 0; z <= cellsZ_; ++z) {
            for (std::size_t x = 0; x <= cellsX_; ++x) {
                float px = (float)x / (float)cellsX_ * size_ - half;
                float pz = (float)z / (float)cellsZ_ * size_ - half;
                verts.push_back(px); verts.push_back(0.0f); verts.push_back(pz);
            }
        }
        std::uint32_t row = (std::uint32_t)(cellsX_ + 1);
        for (std::size_t z = 0; z < cellsZ_; ++z) {
            for (std::size_t x = 0; x < cellsX_; ++x) {
                std::uint32_t i0 = (std::uint32_t)(z*row + x);
                std::uint32_t i1 = (std::uint32_t)(z*row + x + 1);
                std::uint32_t i2 = (std::uint32_t)((z+1)*row + x);
                std::uint32_t i3 = (std::uint32_t)((z+1)*row + x + 1);
                indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
                indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
            }
        }
        return (cellsX_+1) * (cellsZ_+1);
    }

private:
    std::size_t cellsX_ = 8, cellsZ_ = 8;
    float size_ = 1.0f;
};

} // namespace bighero
