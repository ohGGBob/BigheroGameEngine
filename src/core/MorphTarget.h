#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace bighero {

// MorphTarget: a blend-shape that displaces a set of vertices (by index) with
// per-target positions. Holds the vertex indices and displacement deltas.
// Pure CPU-side data container used by the skinning/morph pass.
class MorphTarget {
public:
    struct Delta { std::size_t vertex; float dx, dy, dz; };

    MorphTarget() {}
    explicit MorphTarget(const char* name) : name_(name ? name : "") {}

    void SetName(const char* n) { name_ = n ? n : ""; }
    const char* Name() const { return name_.c_str(); }
    void SetWeight(float w) { weight_ = w < 0 ? 0 : (w > 1 ? 1 : w); }
    float Weight() const { return weight_; }
    void SetIndex(int i) { index_ = i; }
    int Index() const { return index_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    void AddDelta(std::size_t vertex, float dx, float dy, float dz) {
        deltas_.push_back({vertex, dx, dy, dz});
    }
    std::size_t DeltaCount() const { return deltas_.size(); }
    bool GetDelta(std::size_t i, Delta& out) const {
        if (i >= deltas_.size()) return false;
        out = deltas_[i]; return true;
    }
    void Clear() { deltas_.clear(); }

private:
    std::string name_;
    float weight_ = 1.0f;
    int index_ = 0;
    bool enabled_ = true;
    std::vector<Delta> deltas_;
};

} // namespace bighero
