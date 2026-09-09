#pragma once
#include <cstdint>

namespace bighero {

// MeshDrawCall: a single draw-call command descriptor bounding the renderer
// work for one mesh submesh/material combination. Self-contained.
struct MeshDrawCall {
    uint64_t meshId = 0;
    uint32_t submeshIndex = 0;
    uint64_t materialId = 0;
    uint64_t pipeline = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t firstVertex = 0;
    uint32_t firstIndex = 0;
    // Instance count for instanced rendering (0 = non-instanced, 1 = single).
    uint32_t instanceCount = 1;
    // Sorting key for front-to-back / back-to-front ordering.
    int64_t sortKey = 0;

    MeshDrawCall() = default;
    MeshDrawCall(uint64_t mesh, uint32_t submesh, uint64_t material)
        : meshId(mesh), submeshIndex(submesh), materialId(material) {}

    void SetMesh(uint64_t m) { meshId = m; }
    void SetMaterial(uint64_t m) { materialId = m; }
    void SetRange(uint32_t firstV, uint32_t vCount, uint32_t firstI, uint32_t iCount) {
        firstVertex = firstV; vertexCount = vCount;
        firstIndex = firstI; indexCount = iCount;
    }
    bool IsValid() const { return meshId != 0 && (vertexCount > 0 || indexCount > 0); }
};

} // namespace bighero
