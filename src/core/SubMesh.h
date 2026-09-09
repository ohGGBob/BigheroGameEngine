#pragma once
#include <cstddef>
#include <vector>

namespace bighero {

// SubMesh: a subset of a mesh's index buffer, referencing a contiguous range
// of indices and a material id. Pure data container for multi-material meshes.
class SubMesh {
public:
    SubMesh() {}
    SubMesh(std::size_t indexStart, std::size_t indexCount, std::size_t materialId)
        : indexStart_(indexStart), indexCount_(indexCount), materialId_(materialId) {}

    void SetIndexRange(std::size_t start, std::size_t count) {
        indexStart_ = start; indexCount_ = count;
    }
    std::size_t IndexStart() const { return indexStart_; }
    std::size_t IndexCount() const { return indexCount_; }
    void SetMaterialId(std::size_t id) { materialId_ = id; }
    std::size_t MaterialId() const { return materialId_; }
    void SetTopology(int topo) { topology_ = topo; }
    int Topology() const { return topology_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

private:
    std::size_t indexStart_ = 0, indexCount_ = 0, materialId_ = 0;
    int topology_ = 0;
    bool enabled_ = true;
};

} // namespace bighero
