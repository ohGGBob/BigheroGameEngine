#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// MeshResource: describes a mesh's geometry counts + bounding extents.
// Self-contained / std-lib only.
class MeshResource {
public:
    MeshResource() = default;
    MeshResource(std::string name, uint32_t vertexCount, uint32_t indexCount)
        : name_(std::move(name)), vertexCount_(vertexCount), indexCount_(indexCount) {}

    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }
    void SetVertexCount(uint32_t c) { vertexCount_ = c; }
    uint32_t VertexCount() const { return vertexCount_; }
    void SetIndexCount(uint32_t c) { indexCount_ = c; }
    uint32_t IndexCount() const { return indexCount_; }
    void SetPrimitiveCount(uint32_t c) { primitiveCount_ = c; }
    uint32_t PrimitiveCount() const { return primitiveCount_; }
    void SetHasSkinning(bool b) { skinned_ = b; }
    bool HasSkinning() const { return skinned_; }

    void SetBounds(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
        minX_=minX; minY_=minY; minZ_=minZ; maxX_=maxX; maxY_=maxY; maxZ_=maxZ; bounds_=true;
    }
    bool HasBounds() const { return bounds_; }
    float RootOffset() const { return rootOffset_; }

    bool IsValid() const { return vertexCount_ > 0; }
    uint32_t TriangleCount() const { return indexCount_ / 3; }
    uint64_t TotalIndices() const { return indexCount_; }

private:
    std::string name_;
    uint32_t vertexCount_=0, indexCount_=0, primitiveCount_=0;
    float minX_=0,minY_=0,minZ_=0,maxX_=0,maxY_=0,maxZ_=0, rootOffset_=0;
    bool skinned_=false, bounds_=false;
};

} // namespace bighero
