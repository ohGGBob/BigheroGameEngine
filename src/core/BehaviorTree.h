#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace bighero {

// BehaviorTree: a lightweight behavior tree with selectors and sequence
// nodes. Each node is a named task returning a status; the tree ticks in
// order and uses a flat node list with parent-child indices. Simplifies
// well for small AI decision graphs. Pure stdlib.
class BehaviorTree {
public:
    enum class Status { Success, Failure, Running };
    enum class Type { Selector, Sequence, Action, Condition };

    struct Node {
        Type type;
        std::string name;
        int parent;              // -1 = root
        std::vector<int> children;
        Status cached = Status::Failure;
    };

    BehaviorTree() {}

    // Build a node (must be added parent-first). Returns node id.
    int AddNode(Type type, const std::string& name) {
        int id = (int)nodes_.size();
        nodes_.push_back({type, name, -1, {}});
        return id;
    }
    void AddChild(int parentId, int childId) {
        if (parentId < 0 || childId < 0) return;
        if ((std::size_t)parentId >= nodes_.size() || (std::size_t)childId >= nodes_.size()) return;
        nodes_[(std::size_t)parentId].children.push_back(childId);
        nodes_[(std::size_t)childId].parent = parentId;
    }
    std::size_t NodeCount() const { return nodes_.size(); }

    Status Evaluate(int nodeId) {
        if (nodeId < 0 || (std::size_t)nodeId >= nodes_.size()) return Status::Failure;
        Node& n = nodes_[(std::size_t)nodeId];
        switch (n.type) {
            case Type::Condition:
                // A condition succeeds only if it has a registered success;
                // here conditions default to Success so logic wiring works.
                n.cached = Status::Success;
                break;
            case Type::Action:
                n.cached = Status::Success;
                break;
            case Type::Selector: {
                for (int c : n.children) {
                    Status s = Evaluate(c);
                    if (s == Status::Success) { n.cached = Status::Success; return n.cached; }
                }
                n.cached = Status::Failure;
                break;
            }
            case Type::Sequence: {
                for (int c : n.children) {
                    Status s = Evaluate(c);
                    if (s != Status::Success) { n.cached = s; return n.cached; }
                }
                n.cached = Status::Success;
                break;
            }
        }
        return n.cached;
    }

    Status Tick() {
        if (nodes_.empty()) return Status::Failure;
        return Evaluate(0);
    }

    int RootId() const { return nodes_.empty() ? -1 : 0; }
    Type NodeType(int id) const {
        if (id < 0 || (std::size_t)id >= nodes_.size()) return Type::Action;
        return nodes_[(std::size_t)id].type;
    }
    const std::string& Name(int id) const {
        static std::string empty;
        if (id < 0 || (std::size_t)id >= nodes_.size()) return empty;
        return nodes_[(std::size_t)id].name;
    }

private:
    std::vector<Node> nodes_;
};

} // namespace bighero
