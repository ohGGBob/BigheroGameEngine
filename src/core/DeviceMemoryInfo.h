#pragma once
#include <cstdint>

namespace bighero {

// DeviceMemoryInfo: describes a block of device memory (size, type index,
// heap usage). Self-contained, std-lib only.
class DeviceMemoryInfo {
public:
    DeviceMemoryInfo() = default;
    DeviceMemoryInfo(uint64_t size, uint32_t memoryTypeIndex = 0)
        : size_(size), memoryTypeIndex_(memoryTypeIndex) {}

    void SetSize(uint64_t s) { size_ = s; }
    uint64_t Size() const { return size_; }
    void SetMemoryTypeIndex(uint32_t i) { memoryTypeIndex_ = i; }
    uint32_t MemoryTypeIndex() const { return memoryTypeIndex_; }
    void SetHandle(uint64_t h) { handle_ = h; }
    uint64_t Handle() const { return handle_; }

    void SetMapped(bool b) { mapped_ = b; }
    bool IsMapped() const { return mapped_; }
    void SetMappedSize(uint64_t s) { mappedSize_ = s; }
    uint64_t MappedSize() const { return mappedSize_; }

    void SetDedicated(bool b) { dedicated_ = b; }
    bool IsDedicated() const { return dedicated_; }

    bool IsValid() const { return size_ > 0; }
    static const char* HeapName(uint32_t typeIndex) {
        switch (typeIndex) {
            case 0: return "DeviceLocal";
            case 1: return "HostVisible";
            case 2: return "HostCached";
            default: return "Other";
        }
    }

private:
    uint64_t size_ = 0;
    uint32_t memoryTypeIndex_ = 0;
    uint64_t handle_ = 0;
    bool mapped_ = false;
    uint64_t mappedSize_ = 0;
    bool dedicated_ = false;
};

} // namespace bighero
