#pragma once
#include <cstddef>
#include <vector>

namespace bighero {

// SkinnedMesh: a bone-skinned mesh descriptor holding the bind skeleton bone
// count, the per-vertex bone-weight stream, and skinning matrices index base.
// Pure config/data container used by the skinned render pass.
class SkinnedMesh {
public:
    SkinnedMesh() {}
    explicit SkinnedMesh(std::size_t boneCount) : boneCount_(boneCount) {}

    void SetBoneCount(std::size_t n) { boneCount_ = n; }
    std::size_t BoneCount() const { return boneCount_; }
    void SetMeshId(std::size_t id) { meshId_ = id; }
    std::size_t MeshId() const { return meshId_; }
    void SetWeightsStream(std::size_t streamIndex) { weightsStream_ = streamIndex; }
    std::size_t WeightsStream() const { return weightsStream_; }
    void SetBindPoseBase(std::size_t offset) { bindPoseBase_ = offset; }
    std::size_t BindPoseBase() const { return bindPoseBase_; }

    // Root bone index (usually 0) for root-motion / re-rooting.
    void SetRootBone(std::size_t idx) { rootBone_ = idx; }
    std::size_t RootBone() const { return rootBone_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetCastShadows(bool c) { castShadows_ = c; }
    bool CastShadows() const { return castShadows_; }

private:
    std::size_t boneCount_ = 0, meshId_ = 0;
    std::size_t weightsStream_ = 0, bindPoseBase_ = 0, rootBone_ = 0;
    bool enabled_ = true, castShadows_ = true;
};

} // namespace bighero
