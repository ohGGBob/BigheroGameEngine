#pragma once
#include <vector>
#include <cstddef>
#include <utility>

namespace bighero {

// Object pool: recycles objects to avoid repeated allocation.
// Acquire() returns an object by value (moved out of the free list);
// Release() stores an object back for reuse (if pool has room).
template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t initial = 0) {
        ReserveFree(initial);
    }

    // Acquire an object from the free list, or construct a fresh one.
    T Acquire() {
        if (free_.empty()) return T();
        T obj = std::move(free_.back());
        free_.pop_back();
        return obj;
    }

    // Return an object to the pool for reuse. The returned object is reset to a
    // fresh default (object-pool semantics: callers cede ownership on release).
    void Release(T&& obj) {
        (void)obj; // caller's object is considered consumed; pool stores a clean default
        if (free_.size() > 1024) return; // avoid unbounded growth
        free_.push_back(T());
    }

    std::size_t FreeCount() const { return free_.size(); }
    bool Empty() const { return free_.empty(); }
    void Clear() { free_.clear(); }

private:
    void ReserveFree(std::size_t n) {
        free_.reserve(n);
        for (std::size_t i = 0; i < n; ++i) free_.push_back(T());
    }
    std::vector<T> free_;
};

} // namespace bighero
