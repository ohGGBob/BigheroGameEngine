#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <string>

namespace bighero {

// Bones: a flat-armature bone buffer storing world/local matrices and a
// parent hierarchy, used for skinning and animation. Self-contained.
class Bones {
public:
    Bones() = default;
    explicit Bones(int count) { Resize(count); }

    void Resize(int count) {
        matrix_ = std::vector<float>((size_t)(count < 0 ? 0 : count) * 16, 0.0f);
        parent_ = std::vector<int32_t>((size_t)(count < 0 ? 0 : count), -1);
        names_.clear();
        // Identity matrices.
        for (int i = 0; i < count; ++i) {
            SetIdentity(i);
        }
    }
    int Count() const { return (int)parent_.size(); }

    void SetParent(int bone, int parent) {
        if (bone < 0 || bone >= Count()) return;
        parent_[(size_t)bone] = parent;
    }
    int GetParent(int bone) const {
        if (bone < 0 || bone >= Count()) return -1;
        return parent_[(size_t)bone];
    }

    // Compute the depth of a bone in the hierarchy (0 = root).
    int GetDepth(int bone) const {
        int depth = 0;
        int cur = bone;
        while (cur >= 0 && depth < 1024) {
            cur = GetParent(cur);
            if (cur >= 0) ++depth;
        }
        return depth;
    }

    void SetLocalMatrix(int bone, const float* m16) {
        if (bone < 0 || bone >= Count()) return;
        for (int i = 0; i < 16; ++i)
            matrix_[(size_t)bone * 16 + i] = m16[i];
    }
    const float* GetLocalMatrix(int bone) const {
        if (bone < 0 || bone >= Count()) return nullptr;
        return &matrix_[(size_t)bone * 16];
    }

    void SetName(int bone, const char* name) {
        if (bone < 0 || bone >= Count()) return;
        if ((size_t)bone >= names_.size()) names_.resize((size_t)bone + 1);
        names_[(size_t)bone] = name ? name : "";
    }
    const char* GetName(int bone) const {
        if (bone < 0 || bone >= Count()) return "";
        if ((size_t)bone >= names_.size()) return "";
        return names_[(size_t)bone].c_str();
    }

private:
    void SetIdentity(int bone) {
        for (int i = 0; i < 16; ++i) matrix_[(size_t)bone * 16 + i] = 0;
        matrix_[(size_t)bone * 16 + 0] = 1;
        matrix_[(size_t)bone * 16 + 5] = 1;
        matrix_[(size_t)bone * 16 + 10] = 1;
        matrix_[(size_t)bone * 16 + 15] = 1;
    }
    std::vector<float> matrix_;
    std::vector<int32_t> parent_;
    std::vector<std::string> names_;
};

} // namespace bighero
