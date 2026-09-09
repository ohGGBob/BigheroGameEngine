#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// HumanoidPose: a skeletal pose for a humanoid rig — per-bone local
// translations and rotations summarized. Self-contained descriptor that the
// animator/IK uses. Bones are referenced by index; only positions stored.
class HumanoidPose {
public:
    struct BonePose { float tx, ty, tz; };

    explicit HumanoidPose(std::size_t boneCount = 0) { ResizeBones(boneCount); }

    void ResizeBones(std::size_t n) { bones_.assign(n, BonePose{0,0,0}); }
    std::size_t BoneCount() const { return bones_.size(); }
    bool SetBoneTranslation(std::size_t i, float x, float y, float z) {
        if (i >= bones_.size()) return false;
        bones_[i] = {x, y, z}; return true;
    }
    bool GetBoneTranslation(std::size_t i, float& x, float& y, float& z) const {
        if (i >= bones_.size()) return false;
        x=bones_[i].tx; y=bones_[i].ty; z=bones_[i].tz; return true;
    }

    // Blend two poses by weight t into this pose's bone translations.
    void Blend(const HumanoidPose& a, const HumanoidPose& b, float t) {
        std::size_t n = a.BoneCount() < b.BoneCount() ? a.BoneCount() : b.BoneCount();
        ResizeBones(n);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        for (std::size_t i = 0; i < n; ++i) {
            float ax,ay,az,bx,by,bz;
            a.GetBoneTranslation(i, ax,ay,az);
            b.GetBoneTranslation(i, bx,by,bz);
            bones_[i] = {ax+(bx-ax)*t, ay+(by-ay)*t, az+(bz-az)*t};
        }
    }

    // Root motion height delta between two poses (for foot grounding).
    float RootHeight(const HumanoidPose& other) const {
        if (BoneCount() < 1 || other.BoneCount() < 1) return 0.0f;
        return other.bones_[0].ty - bones_[0].ty;
    }

    void Clear() { bones_.clear(); }
    void Normalize() {
        // Clamp each translation to a sane range to avoid runaway values.
        for (auto& b : bones_) {
            if (std::fabs(b.tx) > 1e6f) b.tx = 0;
            if (std::fabs(b.ty) > 1e6f) b.ty = 0;
            if (std::fabs(b.tz) > 1e6f) b.tz = 0;
        }
    }

private:
    std::vector<BonePose> bones_;
};

} // namespace bighero
