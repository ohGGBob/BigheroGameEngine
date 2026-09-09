#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace bighero {

// BoneHierarchy: parent/child relationship storage for a skeleton, resolving
// depth and root membership. Self-contained / std-lib only.
class BoneHierarchy {
public:
    void SetParentCount(size_t n) { parent_.assign(n, -1); }
    void SetParent(size_t bone, int parentBone) {
        if (bone < parent_.size()) parent_[bone] = parentBone;
    }
    int Parent(size_t bone) const { return bone < parent_.size() ? parent_[bone] : -1; }
    size_t Size() const { return parent_.size(); }

    bool IsRoot(size_t bone) const { return bone < parent_.size() && parent_[bone] < 0; }
    size_t RootCount() const {
        size_t n = 0;
        for (int p : parent_) if (p < 0) ++n;
        return n;
    }

    int Depth(size_t bone) const {
        int d = 0; size_t cur = bone;
        while (cur < parent_.size() && parent_[cur] >= 0) { cur = (size_t)parent_[cur]; ++d; if (d > 4096) break; }
        return d;
    }

    std::vector<size_t> Children(size_t bone) const {
        std::vector<size_t> out;
        for (size_t i = 0; i < parent_.size(); ++i) if (parent_[i] == (int)bone) out.push_back(i);
        return out;
    }

    void Clear() { parent_.clear(); }
private:
    std::vector<int> parent_;
};

} // namespace bighero
