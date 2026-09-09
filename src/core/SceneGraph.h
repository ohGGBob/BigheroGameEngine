#pragma once
#include <deque>
#include <cstdint>
#include <cstddef>
#include "SceneNode.h"

namespace bighero {

// SceneGraph: a root-owned collection of SceneNodes. Provides add/find/destroy
// plus hierarchical activation bookkeeping. Pure stdlib container.
class SceneGraph {
public:
    SceneGraph() {}

    SceneNode* CreateRoot(std::uint64_t id, const std::string& name) {
        nodes_.push_back(SceneNode(id, name));
        return &nodes_.back();
    }
    SceneNode* AddChild(SceneNode* parent, std::uint64_t id, const std::string& name) {
        if (!parent) return nullptr;
        nodes_.push_back(SceneNode(id, name));
        SceneNode* child = &nodes_.back();
        child->SetParent(parent);
        parent->AddChild(child);
        return child;
    }
    std::size_t Count() const { return nodes_.size(); }
    SceneNode* At(std::size_t i) const { return i < nodes_.size() ? (SceneNode*)&nodes_[i] : nullptr; }

    SceneNode* FindById(std::uint64_t id) {
        for (auto& n : nodes_) if (n.Id() == id) return &n;
        return nullptr;
    }
    SceneNode* FindByName(const std::string& name) {
        for (auto& n : nodes_) if (n.Name() == name) return &n;
        return nullptr;
    }

    // Rebuild parent pointers and children lists so node state is consistent.
    void RebuildHierarchy() {
        // Children lists are maintained incrementally in CreateRoot/AddChild,
        // so no global rebuild is required here. Kept as a stable API hook.
    }

    void Clear() { nodes_.clear(); }
    bool IsEmpty() const { return nodes_.empty(); }

private:
    std::deque<SceneNode> nodes_;
};

} // namespace bighero
