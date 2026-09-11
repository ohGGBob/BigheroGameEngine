#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// BlendTree: a node in an animation blend tree (blend of children by weight).
// Self-contained / std-lib only.
class BlendTree {
public:
    BlendTree() = default;
    BlendTree(std::string name, float weight = 1.0f)
        : name_(std::move(name)), weight_(weight) {}

    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }
    void SetWeight(float w) { weight_ = w; }
    float Weight() const { return weight_; }

    size_t AddChild(int childIndex) { children_.push_back(childIndex); return children_.size() - 1; }
    const std::vector<int>& Children() const { return children_; }
    size_t ChildCount() const { return children_.size(); }

    void SetClipName(std::string c) { clip_ = std::move(c); }
    const std::string& ClipName() const { return clip_; }
    bool IsLeaf() const { return children_.empty(); }

private:
    std::string name_, clip_;
    float weight_ = 1.0f;
    std::vector<int> children_;
};

} // namespace bighero
