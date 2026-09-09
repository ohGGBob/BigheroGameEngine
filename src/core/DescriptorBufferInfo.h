#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// DescriptorBufferInfo: describes a buffer range bound to a descriptor
// (buffer handle, offset, range). Self-contained, std-lib only.
class DescriptorBufferInfo {
public:
    DescriptorBufferInfo() = default;
    DescriptorBufferInfo(uint64_t buffer, uint64_t offset = 0, uint64_t range = 0)
        : buffer_(buffer), offset_(offset), range_(range) {}

    void SetBuffer(uint64_t b) { buffer_ = b; }
    uint64_t Buffer() const { return buffer_; }
    void SetOffset(uint64_t o) { offset_ = o; }
    uint64_t Offset() const { return offset_; }
    void SetRange(uint64_t r) { range_ = r; }
    uint64_t Range() const { return range_; }

    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }

    bool IsValid() const { return buffer_ != 0; }
    bool IsWholeRange() const { return range_ == 0; }
    uint64_t EndOffset() const { return offset_ + range_; }

private:
    uint64_t buffer_ = 0, offset_ = 0, range_ = 0;
    std::string name_;
};

} // namespace bighero
