#pragma once
#include <cstdint>
#include <algorithm>
#include <atomic>

namespace bighero {

// RefCount: a thread-safe reference counter that invokes a release callback
// when the count drops to zero (e.g. object destruction). Self-contained,
// std-lib only.
class RefCount {
public:
    using ReleaseFn = void(*)(void* userData);

    RefCount() = default;
    explicit RefCount(int initial) : count_(initial) {}
    ~RefCount() = default;

    void Reset(int initial = 0) { count_.store(initial, std::memory_order_release); }
    int Load() const { return count_.load(std::memory_order_acquire); }

    void AddRef() { count_.fetch_add(1, std::memory_order_relaxed); }
    void AddRef(int n) { count_.fetch_add(n, std::memory_order_relaxed); }

    // Release a reference; returns true if the count reached zero.
    bool Release() {
        int prev = count_.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) {
            if (releaseFn_) releaseFn_(userData_);
            return true;
        }
        return false;
    }
    bool IsZero() const { return count_.load(std::memory_order_acquire) <= 0; }
    int StrongCount() const { return count_.load(std::memory_order_acquire); }

    void SetReleaseCallback(ReleaseFn fn, void* userData) {
        releaseFn_ = fn;
        userData_ = userData;
    }

private:
    std::atomic<int> count_{1};
    ReleaseFn releaseFn_ = nullptr;
    void* userData_ = nullptr;
};

} // namespace bighero
