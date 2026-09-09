#pragma once
#include <vector>
#include <string>
#include <memory>
#include <cstddef>
#include "SystemBase_v2.h"

namespace bighero {

// Scene: a container of named nodes organized hierarchically, plus a set of
// systems that run each frame. Building block for a scene graph.
class Scene {
public:
    struct Node {
        std::string name;
        Node* parent = nullptr;
        std::vector<Node> children;
        void* userData = nullptr;
    };

    Scene() {
        // Root node.
        root_.name = "__scene_root__";
    }

    Node* Root() { return &root_; }

    Node* CreateNode(const std::string& name, Node* parent = nullptr) {
        Node n;
        n.name = name;
        n.parent = parent ? parent : &root_;
        (parent ? parent : &root_)->children.push_back(std::move(n));
        return &(parent ? parent : &root_)->children.back();
    }

    std::size_t NodeCount() const { return CountNodes(root_); }
    bool Empty() const { return root_.children.empty(); }

    // Find a node by name (BFS).
    Node* Find(const std::string& name) {
        return FindNode(root_, name);
    }

    void Clear() { root_.children.clear(); systems_.clear(); }

    // --- Systems ---
    std::size_t AddSystem(std::shared_ptr<SystemBase> sys) {
        systems_.push_back(sys);
        return systems_.size() - 1;
    }
    std::size_t SystemCount() const { return systems_.size(); }
    void UpdateSystems(float dt) {
        for (auto& s : systems_) if (s && s->Enabled()) s->Update(dt);
    }

private:
    std::size_t CountNodes(const Node& n) const {
        std::size_t c = 1;
        for (const auto& ch : n.children) c += CountNodes(ch);
        return c;
    }
    Node* FindNode(Node& n, const std::string& name) {
        if (n.name == name) return &n;
        for (auto& ch : n.children) {
            Node* r = FindNode(ch, name);
            if (r) return r;
        }
        return nullptr;
    }

    Node root_;
    std::vector<std::shared_ptr<SystemBase>> systems_;
};

} // namespace bighero
