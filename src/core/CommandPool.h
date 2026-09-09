#pragma once
#include <cstdint>

namespace bighero {

// CommandPool: allocates and manages command buffers from a pool associated
// with a queue family. Self-contained, std-lib only.
class CommandPool {
public:
    enum class ResetFlag : uint8_t { None = 0, ReleaseResources = 1 };

    CommandPool() = default;
    explicit CommandPool(uint32_t queueFamilyIndex, uint32_t flags = 0)
        : queueFamilyIndex_(queueFamilyIndex), flags_(flags) {}

    void SetQueueFamily(uint32_t q) { queueFamilyIndex_ = q; }
    uint32_t QueueFamily() const { return queueFamilyIndex_; }
    void SetFlags(uint32_t f) { flags_ = f; }
    uint32_t Flags() const { return flags_; }

    void Allocate(uint32_t count) { capacity_ += count; allocated_ += count; }
    void FreeBuffers(uint32_t count) {
        if (count >= allocated_) { allocated_ = 0; }
        else { allocated_ -= count; }
    }
    void Reset(ResetFlag /*flag*/ = ResetFlag::ReleaseResources) {
        allocated_ = 0;
        inFlight_ = 0;
    }
    uint32_t Capacity() const { return capacity_; }
    uint32_t Allocated() const { return allocated_; }
    void MarkInFlight(uint32_t count) { inFlight_ += count; }
    void MarkComplete(uint32_t count) { inFlight_ = inFlight_ > count ? inFlight_ - count : 0; }
    uint32_t InFlight() const { return inFlight_; }
    bool IsEmpty() const { return allocated_ == 0; }

private:
    uint32_t queueFamilyIndex_ = 0;
    uint32_t flags_ = 0;
    uint32_t capacity_ = 0;
    uint32_t allocated_ = 0;
    uint32_t inFlight_ = 0;
};

} // namespace bighero
