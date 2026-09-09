#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// QueueFamily: describes a GPU queue family (graphics/compute/transfer) and
// how many queues of that family are available. Self-contained.
class QueueFamily {
public:
    enum class Flag : uint8_t { Graphics = 1, Compute = 2, Transfer = 4, SparseBinding = 8, Protected = 16, Video = 32 };

    QueueFamily() = default;
    QueueFamily(uint32_t familyIndex, uint32_t queueCount, uint32_t flags = 0)
        : familyIndex_(familyIndex), queueCount_(queueCount), flags_(flags) {}

    void SetIndex(uint32_t i) { familyIndex_ = i; }
    uint32_t Index() const { return familyIndex_; }
    void SetQueueCount(uint32_t c) { queueCount_ = c; }
    uint32_t QueueCount() const { return queueCount_; }
    void SetFlags(uint32_t f) { flags_ = f; }
    uint32_t Flags() const { return flags_; }

    bool Has(Flag f) const { return (flags_ & (uint32_t)f) == (uint32_t)f; }
    bool SupportsGraphics() const { return Has(Flag::Graphics); }
    bool SupportsCompute() const { return Has(Flag::Compute); }
    bool SupportsTransfer() const { return Has(Flag::Transfer); }
    bool SupportsSparse() const { return Has(Flag::SparseBinding); }

    // Whether this family can present to a given surface (simplified, true).
    bool SupportsPresent() const { return true; }

    size_t PresentableQueueCount() const { return queueCount_; }

private:
    uint32_t familyIndex_ = 0;
    uint32_t queueCount_ = 1;
    uint32_t flags_ = 0;
};

} // namespace bighero
