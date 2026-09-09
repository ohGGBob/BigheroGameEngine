#pragma once
#include <cstdint>

namespace bighero {

// Semaphore: a GPU synchronization primitive used for queue and swapchain
// synchronization (binary semaphore model). Self-contained, std-lib only.
class Semaphore {
public:
    Semaphore() = default;
    explicit Semaphore(uint64_t initialValue) : value_(initialValue) {}

    void Increment() { ++value_; }
    void Decrement() { if (value_ > 0) --value_; }
    uint64_t Value() const { return value_; }
    bool IsSignaled() const { return value_ > 0; }
    void Reset() { value_ = 0; }

    void Signal(uint64_t stageMask = 0xFFFFFFFFu) { ++value_; lastStageMask_ = stageMask; }
    void Wait(uint64_t stageMask = 0xFFFFFFFFu) {
        if (value_ > 0) --value_;
        lastStageMask_ = stageMask;
    }
    uint64_t LastStageMask() const { return lastStageMask_; }

    void SetMaxValue(uint64_t m) { maxValue_ = m; }
    uint64_t MaxValue() const { return maxValue_; }

private:
    uint64_t value_ = 0;
    uint64_t lastStageMask_ = 0xFFFFFFFFu;
    uint64_t maxValue_ = 0xFFFFFFFFu;
};

} // namespace bighero
