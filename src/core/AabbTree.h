#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// Simple bounding-volume hierarchy (AABB tree) for 2D AABB broad-phase.
// Built from a flat vector of leaf AABBs; stores internal nodes with a
// combined AABB and child indices. Pure standard library, self-contained.
class AabbTree {
public:
    struct Aabb {
        float minx, miny, maxx, maxy;
        Aabb() : minx(0), miny(0), maxx(0), maxy(0) {}
        Aabb(float minx_, float miny_, float maxx_, float maxy_)
            : minx(minx_), miny(miny_), maxx(maxx_), maxy(maxy_) {}
        Aabb Union(const Aabb& o) const {
            return Aabb(std::min(minx,o.minx), std::min(miny,o.miny),
                        std::max(maxx,o.maxx), std::max(maxy,o.maxy));
        }
        bool Overlaps(const Aabb& o) const {
            return !(o.minx > maxx || o.maxx < minx || o.miny > maxy || o.maxy < miny);
        }
    };

    void Clear() { nodes_.clear(); }

    // Insert a leaf AABB + optional payload id.
    void Add(const Aabb& box, int payload = -1) {
        nodes_.push_back(Node{box, (int)nodes_.size(), -1, payload, true});
    }

    // Build the hierarchy (median-based on combined bounds center x).
    void Build() {
        if (nodes_.empty()) return;
        std::vector<int> leaves;
        for (int i = 0; i < (int)nodes_.size(); ++i)
            if (nodes_[i].isLeaf) leaves.push_back(i);
        if (!leaves.empty()) BuildRecursive(leaves, 0, (int)leaves.size());
    }

    bool Empty() const { return nodes_.empty(); }
    std::size_t NodeCount() const { return nodes_.size(); }

    // Query all leaves overlapping 'query'. Appends payload ids to 'out'.
    void Query(const Aabb& query, std::vector<int>& out) const {
        if (nodes_.empty()) return;
        QueryNode(0, query, out);
    }

private:
    struct Node {
        Aabb box;
        int childA;   // for internal nodes: left child index
        int childB;   // for internal nodes: right child index
        int payload;  // for leaves
        bool isLeaf;
    };

    void BuildRecursive(std::vector<int>& leaves, int begin, int end) {
        if (end - begin <= 1) return; // single leaf: already a node
        // Split by combined bounds center x (median).
        int mid = (begin + end) / 2;
        // Create internal node wrapping leaves[begin..end).
        int ia = leaves[begin], ib = leaves[end - 1];
        Aabb combined = nodes_[ia].box;
        for (int i = begin + 1; i < end; ++i)
            combined = combined.Union(nodes_[leaves[i]].box);
        // Internal node id.
        int internalIdx = (int)nodes_.size();
        nodes_.push_back(Node{combined, ia, ib, -1, false});
        // Recurse.
        BuildRecursive(leaves, begin, mid);
        BuildRecursive(leaves, mid, end);
        // Attach top-level internal node at the front (simplify: store at index 0 for query entry).
        if (begin == 0) root_ = (int)nodes_.size() - 1; // last pushed is the top node
    }

    void QueryNode(int idx, const Aabb& q, std::vector<int>& out) const {
        if (idx < 0 || idx >= (int)nodes_.size()) return;
        const Node& n = nodes_[idx];
        if (!n.box.Overlaps(q)) return;
        if (n.isLeaf) {
            out.push_back(n.payload);
            return;
        }
        QueryNode(n.childA, q, out);
        QueryNode(n.childB, q, out);
    }

    std::vector<Node> nodes_;
    int root_ = 0;
};

} // namespace bighero
