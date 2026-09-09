#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace bighero {

// SortingGroup: assigns renderables to a sorting group so that all children
// within the group render contiguously in a chosen order. Tracks group id,
// sorting order, and sort layer. Pure data container.
class SortingGroup {
public:
    SortingGroup() {}
    explicit SortingGroup(std::uint64_t groupId) : groupId_(groupId) {}

    void SetGroupId(std::uint64_t id) { groupId_ = id; }
    std::uint64_t GroupId() const { return groupId_; }
    void SetOrder(int order) { order_ = order; }
    int Order() const { return order_; }
    void SetSortLayer(int layer) { layer_ = layer; }
    int SortLayer() const { return layer_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    // Add a child renderable id (opaque; backend resolves ordering).
    void AddChild(std::uint64_t child) { children_.push_back(child); }
    std::size_t ChildCount() const { return children_.size(); }
    bool GetChild(std::size_t i, std::uint64_t& out) const {
        if (i >= children_.size()) return false;
        out = children_[i]; return true;
    }
    void ClearChildren() { children_.clear(); }

private:
    std::uint64_t groupId_ = 0;
    int order_ = 0;
    int layer_ = 0;
    bool enabled_ = true;
    std::vector<std::uint64_t> children_;
};

} // namespace bighero
