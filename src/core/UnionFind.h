#pragma once
#include <vector>
#include <cstddef>

namespace bighero {

// Disjoint-set (union-find) data structure with path compression + rank.
class UnionFind {
public:
    explicit UnionFind(std::size_t n = 0) { Init(n); }

    void Init(std::size_t n) {
        parent_.resize(n);
        rank_.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i) parent_[i] = (int)i;
        count_ = n;
    }

    int Find(int x) {
        if (x < 0 || x >= (int)parent_.size()) return -1;
        if (parent_[x] != x) parent_[x] = Find(parent_[x]); // path compression
        return parent_[x];
    }
    int Find(int x) const {
        // const version (no compression)
        if (x < 0 || x >= (int)parent_.size()) return -1;
        while (parent_[x] != x) x = parent_[x];
        return x;
    }

    // Union by rank; returns true if merged two distinct sets.
    bool Union(int a, int b) {
        int ra = Find(a), rb = Find(b);
        if (ra < 0 || rb < 0) return false;
        if (ra == rb) return false;
        if (rank_[ra] < rank_[rb]) std::swap(ra, rb);
        parent_[rb] = ra;
        if (rank_[ra] == rank_[rb]) ++rank_[ra];
        --count_;
        return true;
    }

    bool Connected(int a, int b) const { int ra = Find(a), rb = Find(b); return ra >= 0 && ra == rb; }
    std::size_t Count() const { return count_; }
    std::size_t Size() const { return parent_.size(); }

    int Add() {
        parent_.push_back((int)parent_.size());
        rank_.push_back(0);
        ++count_;
        return (int)parent_.size() - 1;
    }

private:
    std::vector<int> parent_;
    std::vector<int> rank_;
    std::size_t count_ = 0;
};

} // namespace bighero
