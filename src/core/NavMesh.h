#pragma once
#include <vector>
#include <cstddef>
#include <cmath>

namespace bighero {

// NavMesh: a simplified navigation mesh — a graph of nodes connected by
// edges. Provides neighbor queries and a greedy path search (no A* here;
// use NavigationPath for the resulting waypoints). Pure stdlib.
class NavMesh {
public:
    struct Node { float x, z; };

    NavMesh() {}

    void AddNode(float x, float z) { nodes_.push_back({x, z}); }
    void Connect(int a, int b, bool bidirectional = true) {
        edges_.push_back({a, b});
        if (bidirectional) edges_.push_back({b, a});
    }
    std::size_t NodeCount() const { return nodes_.size(); }
    std::size_t EdgeCount() const { return edges_.size(); }
    bool GetNode(int i, float& x, float& z) const {
        if (i < 0 || (std::size_t)i >= nodes_.size()) return false;
        x = nodes_[(std::size_t)i].x; z = nodes_[(std::size_t)i].z;
        return true;
    }

    // Greedy nearest-node walk toward a target node; returns next node id.
    int NeighborToward(int fromNode, int toNode) const {
        if (fromNode < 0 || toNode < 0) return -1;
        if ((std::size_t)fromNode >= nodes_.size() || (std::size_t)toNode >= nodes_.size()) return -1;
        if (fromNode == toNode) return toNode;
        const Node& from = nodes_[(std::size_t)fromNode];
        const Node& to = nodes_[(std::size_t)toNode];
        float bestD = Dist(from, to);
        int best = fromNode;
        // examine all nodes (simplification: assume full connectivity via edges)
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            float d = Dist(nodes_[i], to);
            if (d < bestD && i != (std::size_t)toNode) { bestD = d; best = (int)i; }
        }
        return best;
    }

    std::size_t NeighborCount(int node) const {
        std::size_t n = 0;
        for (auto& e : edges_) if (e.first == node) ++n;
        return n;
    }
    bool GetNeighbor(int node, int index, int& out) const {
        std::size_t seen = 0;
        for (auto& e : edges_) {
            if (e.first == node) {
                if ((int)seen == index) { out = e.second; return true; }
                ++seen;
            }
        }
        return false;
    }

    void Clear() { nodes_.clear(); edges_.clear(); }
    bool IsEmpty() const { return nodes_.empty(); }

private:
    struct Edge { int first, second; };
    static float Dist(const Node& a, const Node& b) {
        float dx = a.x - b.x, dz = a.z - b.z;
        return std::sqrt(dx*dx + dz*dz);
    }
    std::vector<Node> nodes_;
    std::vector<Edge> edges_;
};

} // namespace bighero
