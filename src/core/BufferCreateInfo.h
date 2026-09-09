#pragma once
#include <cstdint>

namespace bighero {

// BufferCreateInfo: describes a device buffer (size, usage flags, sharing).
// Self-contained, std-lib only.
class BufferCreateInfo {
public:
    enum Usage : uint16_t {
        TransferSrc = 1, TransferDst = 2, UniformTexel = 4,
        StorageTexel = 8, UniformBuffer = 16, StorageBuffer = 32,
        IndexBuffer = 64, VertexBuffer = 128, IndirectBuffer = 256
    };

    BufferCreateInfo() = default;
    BufferCreateInfo(uint64_t size, uint16_t usage = 0, uint32_t sharing = 0)
        : size_(size), usage_(usage), sharingMode_(sharing) {}

    void SetSize(uint64_t s) { size_ = s; }
    uint64_t Size() const { return size_; }
    void SetUsage(uint16_t u) { usage_ = u; }
    uint16_t UsageFlags() const { return usage_; }
    void SetSharing(uint32_t s) { sharingMode_ = s; }
    uint32_t Sharing() const { return sharingMode_; }

    void AddUsage(Usage u) { usage_ |= static_cast<uint16_t>(u); }
    bool HasUsage(Usage u) const { return (usage_ & static_cast<uint16_t>(u)) != 0; }

    void SetQueueFamilyIndices(const uint32_t* idx, uint32_t count) {
        count_ = count;
        for (uint32_t i = 0; i < count && i < 8; ++i) families_[i] = idx[i];
    }
    uint32_t FamilyCount() const { return count_; }
    uint32_t FamilyAt(uint32_t i) const { return i < 8 ? families_[i] : 0; }

    bool IsValid() const { return size_ > 0; }
    uint64_t TotalBytes() const { return size_; }

private:
    uint64_t size_ = 0;
    uint16_t usage_ = 0;
    uint32_t sharingMode_ = 0;
    uint32_t families_[8] = {0};
    uint32_t count_ = 0;
};

} // namespace bighero
