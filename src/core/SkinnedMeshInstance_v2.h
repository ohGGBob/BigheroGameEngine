#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// SkinnedMeshInstance: (mesh handle + skeleton handle + skinning enable).
// Self-contained / std-lib only.
class SkinnedMeshInstance {
public:
    SkinnedMeshInstance() = default;
    SkinnedMeshInstance(uint32_t mesh, uint32_t skeleton = 0xFFFFFFFFu)
        : mesh_(mesh), skeleton_(skeleton) {}

    void SetMesh(uint32_t m) { mesh_ = m; }
    uint32_t Mesh() const { return mesh_; }
    void SetSkeleton(uint32_t s) { skeleton_ = s; }
    uint32_t Skeleton() const { return skeleton_; }

    void SetSkinningEnabled(bool b) { skinning_ = b; }
    bool SkinningEnabled() const { return skinning_; }
    void EnableSkinning() { skinning_ = true; }

    bool IsValid() const { return mesh_ != 0xFFFFFFFFu; }
    bool HasSkeleton() const { return skeleton_ != 0xFFFFFFFFu; }
    bool IsSkinned() const { return HasSkeleton() && skinning_; }

private:
    uint32_t mesh_ = 0xFFFFFFFFu, skeleton_ = 0xFFFFFFFFu;
    bool skinning_ = false;
};

} // namespace bighero
