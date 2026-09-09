#pragma once
#include <cstdint>

namespace bighero {

// MeshFilter: associates a mesh (by storage id / index) with a renderable
// component; the renderer reads this to pull vertex/index data on the mesh's
// submesh ranges. Self-contained descriptor, std-lib only.
class MeshFilter {
public:
    MeshFilter() = default;
    MeshFilter(uint64_t mesh, uint32_t submesh = 0)
        : meshId_(mesh), submeshIndex_(submesh) {}

    void SetMesh(uint64_t meshId) { meshId_ = meshId; }
    uint64_t GetMesh() const { return meshId_; }
    bool HasMesh() const { return meshId_ != 0; }

    void SetSubmesh(uint32_t idx) { submeshIndex_ = idx; }
    uint32_t GetSubmesh() const { return submeshIndex_; }

    // Material slot id used for submesh rendering (0 = default material).
    void SetMaterialSlot(uint32_t slot) { materialSlot_ = slot; }
    uint32_t GetMaterialSlot() const { return materialSlot_; }

    // Whether the mesh should be rendered as a shadow caster/receiver.
    void SetShadowCasting(bool cast, bool receive) {
        castShadow_ = cast; receiveShadow_ = receive;
    }
    bool CastsShadow() const { return castShadow_; }
    bool ReceivesShadow() const { return receiveShadow_; }

private:
    uint64_t meshId_ = 0;
    uint32_t submeshIndex_ = 0;
    uint32_t materialSlot_ = 0;
    bool castShadow_ = true;
    bool receiveShadow_ = true;
};

} // namespace bighero
