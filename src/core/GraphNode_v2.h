#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// GraphNode: a node in a generic graph with an id, label and outgoing edges
// to other nodes. Self-contained, std-lib only.
class GraphNode {
public:
    GraphNode() = default;
    explicit GraphNode(uint64_t id) : id_(id) {}
    GraphNode(uint64_t id, const char* label) : id_(id), label_(label ? label : "") {}

    uint64_t Id() const { return id_; }
    void SetId(uint64_t id) { id_ = id; }
    const std::string& Label() const { return label_; }
    void SetLabel(const char* label) { label_ = label ? label : ""; }

    void AddNeighbor(uint64_t nodeId) {
        for (auto id : neighbors_) if (id == nodeId) return;
        neighbors_.push_back(nodeId);
    }
    void RemoveNeighbor(uint64_t nodeId) {
        for (size_t i = 0; i < neighbors_.size(); ++i) {
            if (neighbors_[i] == nodeId) { neighbors_.erase(neighbors_.begin()+i); return; }
        }
    }
    bool HasNeighbor(uint64_t nodeId) const {
        for (auto id : neighbors_) if (id == nodeId) return true;
        return false;
    }
    const std::vector<uint64_t>& Neighbors() const { return neighbors_; }
    size_t Degree() const { return neighbors_.size(); }
    void ClearEdges() { neighbors_.clear(); }

    void SetPayload(int64_t p) { payload_ = p; }
    int64_t Payload() const { return payload_; }

private:
    uint64_t id_ = 0;
    std::string label_;
    std::vector<uint64_t> neighbors_;
    int64_t payload_ = 0;
};

} // namespace bighero
