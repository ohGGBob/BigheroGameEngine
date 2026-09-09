#pragma once
#include <vector>
#include <cstddef>
#include <cmath>
#include <algorithm>

namespace bighero {

// BvhBuilder: builds a bounding-volume hierarchy from a set of AABBs using a
// median-split top-down approach. Pure CPU-side data structure builder.
// Self-contained; boxes are [minX,minY,minZ,maxX,maxY,maxZ] float[6].
class BvhBuilder {
public:
    struct Node {
        float minX,minY,minZ,maxX,maxY,maxZ;
        int left, right;      // child node indices, -1 for leaf
        std::size_t start, count;  // primitive range (leaf only)
        bool leaf;
    };

    BvhBuilder() {}

    void Clear() { nodes_.clear(); prims_.clear(); }

    // Build the tree over the given AABB list (each box is 6 floats).
    void Build(const std::vector<float>& boxes) {
        Clear();
        for (std::size_t i = 0; i + 5 < boxes.size(); i += 6) {
            prims_.push_back(i);
            node_boxes_.push_back({boxes[i], boxes[i+1], boxes[i+2],
                                   boxes[i+3], boxes[i+4], boxes[i+5]});
        }
        if (prims_.empty()) return;
        root_ = BuildRec(0, prims_.size());
    }

    std::size_t NodeCount() const { return nodes_.size(); }
    std::size_t PrimitiveCount() const { return prims_.size(); }
    bool GetRoot(std::size_t& idx) const {
        if (nodes_.empty()) return false;
        idx = root_; return true;
    }
    bool GetNode(std::size_t i, Node& out) const {
        if (i >= nodes_.size()) return false;
        out = nodes_[i]; return true;
    }

    // Test if a point is inside any primitive box (linear scan for small sets).
    bool IntersectsAny(float x, float y, float z) const {
        for (std::size_t i = 0; i < node_boxes_.size(); ++i) {
            auto& b = node_boxes_[i];
            if (x >= b.c[0] && x <= b.c[3] && y >= b.c[1] && y <= b.c[4] && z >= b.c[2] && z <= b.c[5])
                return true;
        }
        return false;
    }

private:
    struct Box { float c[6]; };
    int BuildRec(std::size_t begin, std::size_t end) {
        // compute bounds of the range
        Box bound = node_boxes_[prims_[begin]];
        for (std::size_t i = begin+1; i < end; ++i) {
            auto& b = node_boxes_[prims_[i]];
            bound.c[0]=std::min(bound.c[0],b.c[0]); bound.c[1]=std::min(bound.c[1],b.c[1]);
            bound.c[2]=std::min(bound.c[2],b.c[2]); bound.c[3]=std::max(bound.c[3],b.c[3]);
            bound.c[4]=std::max(bound.c[4],b.c[4]); bound.c[5]=std::max(bound.c[5],b.c[5]);
        }
        if (end - begin <= 1) {
            Node n;
            n.minX=bound.c[0]; n.minY=bound.c[1]; n.minZ=bound.c[2];
            n.maxX=bound.c[3]; n.maxY=bound.c[4]; n.maxZ=bound.c[5];
            n.left=n.right=-1; n.start=begin; n.count=end-begin; n.leaf=true;
            nodes_.push_back(n);
            return (int)nodes_.size()-1;
        }
        // split on longest axis at median
        std::size_t mid = begin + (end-begin)/2;
        // find longest axis
        float ex = bound.c[3]-bound.c[0], ey = bound.c[4]-bound.c[1], ez = bound.c[5]-bound.c[2];
        int axis = (ex >= ey && ex >= ez) ? 0 : (ey >= ez ? 1 : 2);
        std::sort(prims_.begin()+begin, prims_.begin()+end,
            [&](std::size_t a, std::size_t b) {
                return node_boxes_[a].c[axis*3] < node_boxes_[b].c[axis*3];   // min on axis
            });
        int leftChild = BuildRec(begin, mid);
        int rightChild = BuildRec(mid, end);
        Node n;
        n.minX=bound.c[0]; n.minY=bound.c[1]; n.minZ=bound.c[2];
        n.maxX=bound.c[3]; n.maxY=bound.c[4]; n.maxZ=bound.c[5];
        n.left=leftChild; n.right=rightChild; n.start=begin; n.count=end-begin; n.leaf=false;
        nodes_.push_back(n);
        return (int)nodes_.size()-1;
    }

    std::vector<Node> nodes_;
    std::vector<std::size_t> prims_;
    std::vector<Box> node_boxes_;
    std::size_t root_ = 0;
};

} // namespace bighero
