#pragma once
#include <cstddef>
#include <cstdint>

namespace bighero {

// BoneWeight: per-vertex skinning weights for up to 4 bone influences.
// Stores indices + normalized weights. Pure data record for the skinning pass.
class BoneWeight {
public:
    BoneWeight() {}

    void Set(std::size_t b0, std::size_t b1, std::size_t b2, std::size_t b3,
             float w0, float w1, float w2, float w3) {
        bone_[0]=b0; bone_[1]=b1; bone_[2]=b2; bone_[3]=b3;
        weight_[0]=w0; weight_[1]=w1; weight_[2]=w2; weight_[3]=w3;
    }
    std::size_t Bone(int i) const { return (i>=0 && i<4) ? bone_[i] : 0; }
    float Weight(int i) const { return (i>=0 && i<4) ? weight_[i] : 0.0f; }

    // Shorter accessors (index-based, 0-3).
    void SetBone(int i, std::size_t b) { if (i>=0&&i<4) bone_[i]=b; }
    void SetWeight(int i, float w) { if (i>=0&&i<4) weight_[i]=w; }

    void Normalize() {
        float sum = weight_[0]+weight_[1]+weight_[2]+weight_[3];
        if (sum <= 0.0f) { weight_[0]=1.0f; weight_[1]=weight_[2]=weight_[3]=0.0f; return; }
        weight_[0]/=sum; weight_[1]/=sum; weight_[2]/=sum; weight_[3]/=sum;
    }
    bool IsZero() const {
        return weight_[0]==0.0f && weight_[1]==0.0f &&
               weight_[2]==0.0f && weight_[3]==0.0f;
    }
    void Reset() { Set(0,0,0,0, 0,0,0,0); weight_[0]=1.0f; }

private:
    std::size_t bone_[4] = {0,0,0,0};
    float weight_[4] = {1,0,0,0};
};

} // namespace bighero
