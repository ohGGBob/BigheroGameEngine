#pragma once
#include <cstddef>
#include <vector>

namespace bighero {

// Sparse set: dense array + sparse index for O(1) insert/remove/contains.
// Stores int values (typical ECS entity ids).
class SparseArray {
public:
    SparseArray() {}
    explicit SparseArray(std::size_t maxId) { dense_.reserve(maxId); sparse_.assign(maxId, npos); }

    void Reserve(std::size_t n) { sparse_.resize(n, npos); }

    // Insert value v (id). Returns false if already present.
    bool Insert(int v) {
        if (v < 0 || (std::size_t)v >= sparse_.size()) sparse_.resize(v + 1, npos);
        if (sparse_[v] != npos) return false; // already present
        sparse_[v] = (int)dense_.size();
        dense_.push_back(v);
        return true;
    }

    bool Contains(int v) const {
        return v >= 0 && (std::size_t)v < sparse_.size() && sparse_[v] != npos;
    }

    bool Remove(int v) {
        if (!Contains(v)) return false;
        int idx = sparse_[v];
        int last = dense_.back();
        dense_[idx] = last;
        sparse_[last] = idx;
        dense_.pop_back();
        sparse_[v] = npos;
        return true;
    }

    std::size_t Size() const { return dense_.size(); }
    bool Empty() const { return dense_.empty(); }
    void Clear() { dense_.clear(); for (auto& s : sparse_) s = npos; }

    int operator[](std::size_t i) const { return dense_[i]; }
    const std::vector<int>& Values() const { return dense_; }

private:
    static constexpr int npos = -1;
    std::vector<int> dense_;
    std::vector<int> sparse_;
};

} // namespace bighero
