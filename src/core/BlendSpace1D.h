#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// BlendSpace1D: a 1-D blend space mapping a scalar parameter to one or two
// neighbouring animation clips with normalized weights. Standard-library only,
// self-contained.
class BlendSpace1D {
public:
    struct Clip { float param; float weight; unsigned id; };

    BlendSpace1D() {}

    void Clear() { clips_.clear(); }
    void AddClip(float param, unsigned id) {
        // Keep sorted by param.
        std::size_t i = 0;
        while (i < clips_.size() && clips_[i].param < param) ++i;
        clips_.insert(clips_.begin() + i, {param, 0.0f, id});
    }
    std::size_t ClipCount() const { return clips_.size(); }

    // Compute weights for a given parameter. Fills clip ids + weights that
    // are non-zero, linearly blending between the two adjacent clips.
    void Sample(float param, std::vector<unsigned>& ids,
                std::vector<float>& weights) const {
        ids.clear(); weights.clear();
        if (clips_.empty()) return;
        if (clips_.size() == 1) { ids.push_back(clips_[0].id); weights.push_back(1.0f); return; }
        if (param <= clips_.front().param) {
            ids.push_back(clips_.front().id); weights.push_back(1.0f); return;
        }
        if (param >= clips_.back().param) {
            ids.push_back(clips_.back().id); weights.push_back(1.0f); return;
        }
        // Find the interval containing param.
        for (std::size_t i = 0; i+1 < clips_.size(); ++i) {
            float p0 = clips_[i].param, p1 = clips_[i+1].param;
            if (param >= p0 && param <= p1) {
                float t = (p1 - p0) > 1e-6f ? (param - p0) / (p1 - p0) : 0.0f;
                if (t < 0) t = 0;
                if (t > 1) t = 1;
                ids.push_back(clips_[i].id);       weights.push_back(1.0f - t);
                ids.push_back(clips_[i+1].id);     weights.push_back(t);
                return;
            }
        }
    }

private:
    std::vector<Clip> clips_;
};

} // namespace bighero
