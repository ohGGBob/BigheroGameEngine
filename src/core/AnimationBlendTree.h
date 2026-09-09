#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// AnimationBlendTree: a tree of animation clips blended by weights. Supports
// a simple 2-input normalized blend with a blend parameter. Pure data + blend
// helper; clips are referenced by id.
class AnimationBlendTree {
public:
    struct ClipRef { std::size_t clipId; float weight; };

    AnimationBlendTree() {}

    void SetBlendParameter(float p) { blendParam_ = Clamp01(p); }
    float BlendParameter() const { return blendParam_; }

    std::size_t AddClip(std::size_t clipId, float defaultWeight) {
        clips_.push_back({clipId, defaultWeight});
        return clips_.size() - 1;
    }
    std::size_t ClipCount() const { return clips_.size(); }
    bool GetClip(std::size_t i, ClipRef& out) const {
        if (i >= clips_.size()) return false;
        out = clips_[i]; return true;
    }
    void SetClipWeight(std::size_t i, float w) {
        if (i < clips_.size()) clips_[i].weight = w < 0 ? 0 : w;
    }

    // Normalized weights summing to 1 (zero-length safely returns 1.0 for one clip).
    std::size_t NormalizedWeights(std::vector<float>& out) const {
        out.assign(clips_.size(), 0.0f);
        if (clips_.empty()) return 0;
        float sum = 0.0f;
        for (auto& c : clips_) sum += c.weight;
        if (sum <= 1e-8f) { out[0] = 1.0f; return 1; }
        for (std::size_t i = 0; i < clips_.size(); ++i) out[i] = clips_[i].weight / sum;
        return clips_.size();
    }

    void Clear() { clips_.clear(); }

private:
    static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    std::vector<ClipRef> clips_;
    float blendParam_ = 0.0f;
};

} // namespace bighero
