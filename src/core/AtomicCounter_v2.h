#pragma once
#include <cstdint>
#include <atomic>

namespace bighero {

// AtomicCounter: a thin wrapper around std::atomic<T> providing increment,
// decrement, CAS and integer conversions. Self-contained, std-lib only.
template <class T = int>
class AtomicCounter {
public:
    AtomicCounter() = default;
    explicit AtomicCounter(T init) : val_(init) {}

    T Add(T d) { return val_.fetch_add(d, std::memory_order_relaxed) + d; }
    T Sub(T d) { return val_.fetch_sub(d, std::memory_order_relaxed) - d; }
    T Increment() { return Add(1); }
    T Decrement() { return Sub(1); }
    T Load() const { return val_.load(std::memory_order_acquire); }
    void Store(T v) { val_.store(v, std::memory_order_release); }
    T Exchange(T v) { return val_.exchange(v, std::memory_order_acq_rel); }

    // Atomic compare-and-swap; returns true if swapped.
    bool CompareAndSwap(T expected, T desired) {
        return val_.compare_exchange_weak(expected, desired,
                                          std::memory_order_acq_rel,
                                          std::memory_order_relaxed);
    }
    bool CompareAndSwapStrong(T expected, T desired) {
        return val_.compare_exchange_strong(expected, desired,
                                            std::memory_order_acq_rel,
                                            std::memory_order_relaxed);
    }

    // If value is zero, increment and return true (successful claim).
    bool TryIncrementFromZero() {
        T zero = 0;
        return val_.compare_exchange_strong(zero, (T)1,
                                            std::memory_order_acq_rel,
                                            std::memory_order_relaxed);
    }

    operator T() const { return Load(); }
    AtomicCounter& operator++() { Increment(); return *this; }
    AtomicCounter& operator--() { Decrement(); return *this; }
    AtomicCounter& operator+=(T d) { Add(d); return *this; }
    AtomicCounter& operator-=(T d) { Sub(d); return *this; }

private:
    std::atomic<T> val_{0};
};

} // namespace bighero
