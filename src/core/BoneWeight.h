#pragma once
#include <cstdint>
#include <array>

namespace bighero {

// BoneWeight: per-vertex influence of up to 4 bones (index + weight pairs).
// Self-contained / std-lib only.
class BoneWeight {
public:
    BoneWeight() = default;

    void Set(uint32_t boneIndex, float weight) {
        for (int i = 0; i < 4; ++i) {
            if (bones_[i] == boneIndex || weights_[i] == 0.0f) {
                bones_[i] = boneIndex; weights_[i] = weight; return;
            }
        }
    }
    uint32_t Bone(int i) const { return i >= 0 && i < 4 ? bones_[i] : 0; }
    float Weight(int i) const { return i >= 0 && i < 4 ? weights_[i] : 0.0f; }
    const std::array<uint32_t,4>& Bones() const { return bones_; }
    const std::array<float,4>& Weights() const { return weights_; }

    float TotalWeight() const { return weights_[0]+weights_[1]+weights_[2]+weights_[3]; }
    bool IsNormalized() const { return TotalWeight() > 0.999f && TotalWeight() < 1.001f; }
    void Normalize() {
        float t = TotalWeight();
        if (t > 0.0f) for (auto& w : weights_) w /= t;
    }
    bool HasInfluence() const { return TotalWeight() > 0.0f; }

private:
    std::array<uint32_t,4> bones_ = {0,0,0,0};
    std::array<float,4> weights_ = {0,0,0,0};
};

} // namespace bighero
