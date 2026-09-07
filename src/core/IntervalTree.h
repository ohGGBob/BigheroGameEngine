#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace bighero {

// Static interval tree for 1D range queries over [low, high] intervals.
// Entries carry a payload id. Build once, then query for overlaps.
class IntervalTree {
public:
    struct Entry { float low, high; int id; };

    void Build(const std::vector<Entry>& entries) {
        entries_ = entries;
        // simple sorted-by-low storage + nested structure for balanced query
        // For a compact self-contained implementation, sort by low and do
        // linear-ish overlap scan via binary search on start upper bound.
        std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b){
            if (a.low != b.low) return a.low < b.low;
            return a.high < b.high;
        });
    }

    // Return ids of all entries whose interval overlaps [qLow, qHigh].
    void Query(float qLow, float qHigh, std::vector<int>& out) const {
        out.clear();
        if (entries_.empty()) return;
        // Find first entry whose low could still be <= qHigh.
        // All entries with low > qHigh cannot overlap.
        // Binary search upper bound on low.
        int lo = 0, hi = (int)entries_.size();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (entries_[mid].low <= qHigh) lo = mid + 1;
            else hi = mid;
        }
        int upper = lo; // first index with low > qHigh
        for (int i = 0; i < upper; ++i) {
            if (entries_[i].high >= qLow && entries_[i].low <= qHigh)
                out.push_back(entries_[i].id);
        }
    }

    // Query all intervals that contain a single point.
    void PointQuery(float p, std::vector<int>& out) const { Query(p, p, out); }

    std::size_t Size() const { return entries_.size(); }
    bool Empty() const { return entries_.empty(); }

private:
    std::vector<Entry> entries_;
};

} // namespace bighero
