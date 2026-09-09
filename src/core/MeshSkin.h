#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

namespace bighero {

// MeshSkin: holds skin binding data (bone indices + weights per vertex) used
// by the skinned mesh pipeline. Self-contained, std-lib only.
class MeshSkin {
public:
    MeshSkin() = default;

    // Set the number of bones referenced by this skin.
    void SetBoneCount(int count) {
        boneCount_ = count;
        boneMatrices_.assign((size_t)count * 16, 0.0f);
        for (int i = 0; i < count; ++i) boneMatrices_[(size_t)i * 16 + 0] = 1;
        for (int i = 0; i < count; ++i) boneMatrices_[(size_t)i * 16 + 5] = 1;
        for (int i = 0; i < count; ++i) boneMatrices_[(size_t)i * 16 + 10] = 1;
        for (int i = 0; i < count; ++i) boneMatrices_[(size_t)i * 16 + 15] = 1;
    }
    int GetBoneCount() const { return boneCount_; }

    // Set a bone's 4x4 (16 floats, row-major) matrix.
    void SetBoneMatrix(int bone, const float* m16) {
        if (bone < 0 || bone >= boneCount_) return;
        for (int i = 0; i < 16; ++i)
            boneMatrices_[(size_t)bone * 16 + i] = m16[i];
    }
    const float* GetBoneMatrix(int bone) const {
        if (bone < 0 || bone >= boneCount_) return nullptr;
        return &boneMatrices_[(size_t)bone * 16];
    }

    // Add a skin weight (bone index + weight) to the current weight set.
    void AddWeight(int vertexIndex, int bone, float weight) {
        if (vertexIndex < 0 || bone < 0) return;
        if ((size_t)vertexIndex >= weights_.size())
            weights_.resize((size_t)vertexIndex + 1);
        Weight w; w.bone = bone; w.weight = weight;
        weights_[(size_t)vertexIndex].push_back(w);
    }
    size_t WeightCount(int vertexIndex) const {
        if (vertexIndex < 0 || (size_t)vertexIndex >= weights_.size()) return 0;
        return weights_[(size_t)vertexIndex].size();
    }
    void GetWeight(int vertexIndex, int slot, int& bone, float& weight) const {
        bone = 0; weight = 0;
        if (vertexIndex < 0 || (size_t)vertexIndex >= weights_.size()) return;
        const auto& set = weights_[(size_t)vertexIndex];
        if (slot < 0 || (size_t)slot >= set.size()) return;
        bone = set[(size_t)slot].bone; weight = set[(size_t)slot].weight;
    }

private:
    struct Weight { int bone = 0; float weight = 0; };
    int boneCount_ = 0;
    std::vector<float> boneMatrices_;
    std::vector<std::vector<Weight>> weights_;
};

} // namespace bighero
