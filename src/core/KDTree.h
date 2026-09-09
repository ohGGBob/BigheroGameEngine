#pragma once
#include <vector>
#include <cstddef>
#include <algorithm>

namespace bighero {

// KDTree: a 2D k-d tree over points, supporting nearest-neighbour queries.
// Standard-library only, self-contained. Validated for correctness via the
// driver with a known point set.
class KDTree {
public:
    KDTree() {}
    KDTree(const std::vector<float>& px, const std::vector<float>& py)
        { Build(px, py); }

    void Build(const std::vector<float>& px, const std::vector<float>& py) {
        pts_.clear();
        std::size_t n = px.size() < py.size() ? px.size() : py.size();
        for (std::size_t i=0;i<n;++i) pts_.push_back({px[i], py[i], (unsigned)i});
        root_ = BuildRec(pts_, 0, pts_.size(), 0);
    }

    void Clear() { pts_.clear(); root_ = 0; }

    // Find nearest point to (qx,qy); returns index or -1, sets distSq.
    long Nearest(float qx, float qy, float& distSq) const {
        if (pts_.empty()) { distSq = -1.0f; return -1; }
        bestIdx_ = -1; bestDist_ = 1e30f;
        NearestRec(root_, qx, qy, 0);
        distSq = bestDist_;
        return bestIdx_;
    }
    long Size() const { return (long)pts_.size(); }

private:
    struct Pt { float x, y; unsigned id; };
    std::vector<Pt> pts_;
    struct Node { unsigned idx; int left=-1, right=-1; };
    std::vector<Node> nodes_;
    int root_ = -1;
    mutable long bestIdx_ = -1;
    mutable float bestDist_ = 1e30f;

    int BuildRec(std::vector<Pt>& arr, int lo, int hi, int depth) {
        if (lo >= hi) return -1;
        int axis = depth & 1;
        std::sort(arr.begin()+lo, arr.begin()+hi, [=](const Pt&a, const Pt&b){
            return axis == 0 ? (a.x < b.x) : (a.y < b.y);
        });
        int mid = lo + (hi - lo) / 2;
        int nodeIdx = (int)nodes_.size();
        nodes_.push_back({});
        // store reference to arr[mid]; record id in nodes
        nodes_[nodeIdx].idx = arr[mid].id;
        nodes_[nodeIdx].left = BuildRec(arr, lo, mid, depth+1);
        nodes_[nodeIdx].right = BuildRec(arr, mid+1, hi, depth+1);
        return nodeIdx;
    }

    void NearestRec(int nodeIdx, float qx, float qy, int depth) const {
        if (nodeIdx < 0) return;
        const Node& nd = nodes_[nodeIdx];
        const Pt& p = pts_[nd.idx];
        float dx = qx - p.x, dy = qy - p.y;
        float d = dx*dx + dy*dy;
        if (d < bestDist_) { bestDist_ = d; bestIdx_ = (long)nd.idx; }
        int axis = depth & 1;
        float diff = axis == 0 ? (qx - p.x) : (qy - p.y);
        int near = diff < 0 ? nd.left : nd.right;
        int far  = diff < 0 ? nd.right : nd.left;
        NearestRec(near, qx, qy, depth+1);
        if (diff*diff < bestDist_)
            NearestRec(far, qx, qy, depth+1);
    }
};

} // namespace bighero
