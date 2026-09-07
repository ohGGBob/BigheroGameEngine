#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// Lightweight transform hierarchy: a forest of nodes with parent/child links
// and per-node local transform (for scene graph / animation pose storage).
class TransformHierarchy {
public:
    struct Node {
        int parent = -1;
        std::vector<int> children;
        float tx = 0, ty = 0, tz = 0;     // local translation
        float rx = 0, ry = 0, rz = 0;     // local rotation (euler radians)
        float sx = 1, sy = 1, sz = 1;     // local scale
        int depth = 0;
    };

    int CreateNode(int parent = -1, float tx = 0, float ty = 0, float tz = 0) {
        if (parent >= (int)nodes_.size()) parent = -1;
        Node n;
        n.parent = parent;
        n.tx = tx; n.ty = ty; n.tz = tz;
        int id = (int)nodes_.size();
        nodes_.push_back(n);
        if (parent >= 0) {
            nodes_[parent].children.push_back(id);
            nodes_[id].depth = nodes_[parent].depth + 1;
        }
        return id;
    }

    // Reparent a node (removes from old parent, adds to new).
    bool Reparent(int node, int newParent) {
        if (node < 0 || node >= (int)nodes_.size()) return false;
        if (newParent == node) return false;
        if (newParent >= (int)nodes_.size()) newParent = -1;
        // avoid creating a cycle
        if (IsAncestor(node, newParent)) return false;
        int old = nodes_[node].parent;
        if (old >= 0) RemoveChild(old, node);
        nodes_[node].parent = newParent;
        if (newParent >= 0) {
            nodes_[newParent].children.push_back(node);
            nodes_[node].depth = nodes_[newParent].depth + 1;
            UpdateDepths(node);
        } else {
            nodes_[node].depth = 0;
            UpdateDepths(node);
        }
        return true;
    }

    Node& Get(int node) { return nodes_[node]; }
    const Node& Get(int node) const { return nodes_[node]; }
    int Parent(int node) const { return nodes_[node].parent; }
    const std::vector<int>& Children(int node) const { return nodes_[node].children; }
    int Depth(int node) const { return nodes_[node].depth; }
    std::size_t Size() const { return nodes_.size(); }

    // Compute world translation by accumulating parents' local translation.
    void GetWorldTranslation(int node, float& wx, float& wy, float& wz) const {
        wx = wy = wz = 0;
        // walk up, then apply? Simple accumulate from root down is complex; do iterative reverse.
        // Collect chain of ancestors from root to node.
        std::vector<int> chain;
        int cur = node;
        while (cur >= 0) { chain.push_back(cur); cur = nodes_[cur].parent; }
        for (int i = (int)chain.size() - 1; i >= 0; --i) {
            const Node& n = nodes_[chain[i]];
            wx += n.tx; wy += n.ty; wz += n.tz;
        }
    }

private:
    bool IsAncestor(int a, int b) const {
        int cur = b;
        while (cur >= 0) { if (cur == a) return true; cur = nodes_[cur].parent; }
        return false;
    }
    void RemoveChild(int parent, int child) {
        auto& c = nodes_[parent].children;
        c.erase(std::remove(c.begin(), c.end(), child), c.end());
    }
    void UpdateDepths(int node) {
        for (int c : nodes_[node].children) {
            nodes_[c].depth = nodes_[node].depth + 1;
            UpdateDepths(c);
        }
    }
    std::vector<Node> nodes_;
};

} // namespace bighero
