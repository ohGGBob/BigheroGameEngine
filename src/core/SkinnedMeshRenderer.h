#pragma once
#include "Renderer.h"
#include <cmath>

namespace bighero {

// SkinnedMeshRenderer: a skinned mesh drawable descriptor that references a
// skeleton (bone count) and provides blend-shape / skinning weight plumbing.
// Self-contained, depends only on Renderer.
class SkinnedMeshRenderer : public Renderer {
public:
    SkinnedMeshRenderer() = default;

    void SetSkeleton(uint64_t skeletonId, int boneCount) {
        skeletonId_ = skeletonId;
        boneCount_ = boneCount;
    }
    uint64_t GetSkeleton() const { return skeletonId_; }
    int GetBoneCount() const { return boneCount_; }

    // Enables/disables GPU skinning (false = CPU skinning).
    void SetGPUSkinning(bool gpu) { gpuSkinning_ = gpu; }
    bool IsGPUSkinning() const { return gpuSkinning_; }

    // Blend-shape weight for a named shape index (0..count-1).
    void SetBlendShapeWeight(int index, float weight) {
        if (index < 0 || index >= 64) return;
        if (weight < 0) weight = 0;
        if (weight > 1) weight = 1;
        blendWeights_[index] = weight;
    }
    float GetBlendShapeWeight(int index) const {
        if (index < 0 || index >= 64) return 0;
        return blendWeights_[index];
    }

    // Skin sphere bounds for frustum culling.
    void SetBounds(float cx, float cy, float cz, float radius) {
        boundsCX_ = cx; boundsCY_ = cy; boundsCZ_ = cz; boundsRadius_ = radius;
    }
    void GetBounds(float& cx, float& cy, float& cz, float& radius) const {
        cx = boundsCX_; cy = boundsCY_; cz = boundsCZ_; radius = boundsRadius_;
    }

private:
    uint64_t skeletonId_ = 0;
    int boneCount_ = 0;
    bool gpuSkinning_ = true;
    float blendWeights_[64] = {0};
    float boundsCX_ = 0, boundsCY_ = 0, boundsCZ_ = 0, boundsRadius_ = 1;
};

} // namespace bighero
