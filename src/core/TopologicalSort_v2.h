#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace bighero {

// TopologicalSort: produces a valid topological ordering of a directed acyclic
// graph given node ids and edge lists. Returns false if a cycle exists.
// Self-contained, std-lib only.
class TopologicalSort {
public:
    // nodes: all node ids; edges: pairs (from, to).
    // On success fills 'order' with a valid ordering and returns true.
    static bool Sort(const std::vector<uint64_t>& nodes,
                     const std::vector<std::pair<uint64_t,uint64_t>>& edges,
                     std::vector<uint64_t>& order) {
        order.clear();
        std::unordered_map<uint64_t, uint32_t> indeg;
        std::unordered_map<uint64_t, std::vector<uint64_t>> adj;
        for (auto n : nodes) indeg[n] = 0;
        for (auto& e : edges) {
            ++indeg[e.second];
            adj[e.first].push_back(e.second);
        }
        // Kahn's algorithm.
        std::vector<uint64_t> ready;
        for (auto& kv : indeg) if (kv.second == 0) ready.push_back(kv.first);
        size_t count = 0;
        while (!ready.empty()) {
            uint64_t n = ready.back(); ready.pop_back();
            order.push_back(n);
            ++count;
            for (auto m : adj[n]) {
                if (--indeg[m] == 0) ready.push_back(m);
            }
        }
        return count == nodes.size(); // false if cycle
    }

    // Convenience: sort a small node list given as 1..N edges.
    static bool SortEdges(const std::vector<std::pair<uint64_t,uint64_t>>& edges,
                          std::vector<uint64_t>& order) {
        std::vector<uint64_t> nodes;
        std::unordered_map<uint64_t, bool> seen;
        for (auto& e : edges) { if (!seen[e.first]) { seen[e.first]=true; nodes.push_back(e.first); }
                                if (!seen[e.second]) { seen[e.second]=true; nodes.push_back(e.second); } }
        return Sort(nodes, edges, order);
    }
};

} // namespace bighero
