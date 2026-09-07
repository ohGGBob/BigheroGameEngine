#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace bighero {

// A chunk of terrain: a grid of height samples plus per-vertex material index
// and normals. Building block for heightfield-based terrain rendering.
class TerrainChunk {
public:
    TerrainChunk() {}
    TerrainChunk(int sizeX, int sizeZ, float cellSize = 1.0f)
        : sizeX_(sizeX), sizeZ_(sizeZ), cellSize_(cellSize) {
        heights_.assign((std::size_t)sizeX * sizeZ, 0.0f);
        materials_.assign((std::size_t)sizeX * sizeZ, 0);
    }

    void Resize(int sizeX, int sizeZ, float cellSize = 1.0f) {
        sizeX_ = sizeX; sizeZ_ = sizeZ; cellSize_ = cellSize;
        heights_.assign((std::size_t)sizeX * sizeZ, 0.0f);
        materials_.assign((std::size_t)sizeX * sizeZ, 0);
    }

    int GridX() const { return sizeX_; }
    int GridZ() const { return sizeZ_; }
    float CellSize() const { return cellSize_; }
    std::size_t SampleCount() const { return heights_.size(); }

    void SetHeight(int x, int z, float h) {
        if (x >= 0 && x < sizeX_ && z >= 0 && z < sizeZ_)
            heights_[(std::size_t)z * sizeX_ + x] = h;
    }
    float GetHeight(int x, int z) const {
        if (x < 0 || x >= sizeX_ || z < 0 || z >= sizeZ_) return 0;
        return heights_[(std::size_t)z * sizeX_ + x];
    }
    void SetMaterial(int x, int z, int m) {
        if (x >= 0 && x < sizeX_ && z >= 0 && z < sizeZ_)
            materials_[(std::size_t)z * sizeX_ + x] = m;
    }
    int GetMaterial(int x, int z) const {
        if (x < 0 || x >= sizeX_ || z < 0 || z >= sizeZ_) return 0;
        return materials_[(std::size_t)z * sizeX_ + x];
    }

    float MinHeight() const {
        if (heights_.empty()) return 0;
        float m = heights_[0];
        for (float h : heights_) if (h < m) m = h;
        return m;
    }
    float MaxHeight() const {
        if (heights_.empty()) return 0;
        float m = heights_[0];
        for (float h : heights_) if (h > m) m = h;
        return m;
    }

    void WorldPos(int x, int z, float& wx, float& wz) const {
        wx = x * cellSize_;
        wz = z * cellSize_;
    }

private:
    int sizeX_ = 0, sizeZ_ = 0;
    float cellSize_ = 1.0f;
    std::vector<float> heights_;
    std::vector<int> materials_;
};

} // namespace bighero
