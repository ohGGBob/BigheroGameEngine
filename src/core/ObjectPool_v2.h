#pragma once
#include <cstdint>
#include <vector>
#include <memory>

namespace bighero {

// ObjectPool: a fixed/reusable object pool that avoids allocation churn.
// Objects are owned by the pool (owned_); a free list of raw pointers tracks
// available instances. Alloc borrows one, Release resets and returns it.
// Self-contained, std-lib only.
template <class T>
class ObjectPool {
public:
    ObjectPool() = default;
    explicit ObjectPool(size_t capacity) { Reserve(capacity); }
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ~ObjectPool() = default;

    // Pre-create n default objects into the free list.
    void Reserve(size_t n) {
        for (size_t i = 0; i < n; ++i) {
            owned_.push_back(std::make_unique<T>());
            free_.push_back(owned_.back().get());
        }
    }

    // Borrow a ready object (freshly default-constructed).
    T* Alloc() {
        if (free_.empty()) {
            owned_.push_back(std::make_unique<T>());
            free_.push_back(owned_.back().get());
        }
        T* obj = free_.back();
        free_.pop_back();
        return obj;
    }

    // Return an object to the pool, resetting it to a default state.
    void Release(T* obj) {
        if (!obj) return;
        *obj = T();
        free_.push_back(obj);
    }

    size_t Available() const { return free_.size(); }
    size_t Owned() const { return owned_.size(); }
    size_t Borrowed() const { return owned_.size() - free_.size(); }

    // Return all owned objects to the free list (resetting each).
    void ResetAll() {
        free_.clear();
        for (auto& o : owned_) free_.push_back(o.get());
    }

private:
    std::vector<std::unique_ptr<T>> owned_;
    std::vector<T*> free_;
};

} // namespace bighero
