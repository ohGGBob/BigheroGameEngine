#pragma once
#include <cstdint>
#include <atomic>

namespace bighero {

// SpinLock: a simple test-and-set spin lock backed by std::atomic. Use for
// short critical sections; not reentrant. Self-contained, std-lib only.
class SpinLock {
public:
    SpinLock() = default;
    SpinLock(const SpinLock&) = delete;
    SpinLock& operator=(const SpinLock&) = delete;

    void Lock() {
        bool expected = false;
        while (!locked_.compare_exchange_weak(expected, true,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
            // Spin (optionally yield).
            expected = false;
        }
    }
    void Unlock() {
        locked_.store(false, std::memory_order_release);
    }
    bool TryLock() {
        bool expected = false;
        return locked_.compare_exchange_weak(expected, true,
                                             std::memory_order_acquire,
                                             std::memory_order_relaxed);
    }

private:
    std::atomic<bool> locked_{false};
};

} // namespace bighero
