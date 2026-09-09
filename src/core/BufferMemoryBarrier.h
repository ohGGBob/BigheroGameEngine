#pragma once
#include <cstdint>

namespace bighero {

// BufferMemoryBarrier: a synchronization barrier for a device buffer (src/dst
// access masks, source/destination stage masks). Self-contained.
class BufferMemoryBarrier {
public:
    BufferMemoryBarrier() = default;
    BufferMemoryBarrier(uint16_t srcAccess, uint16_t dstAccess, uint16_t srcStage,
                        uint16_t dstStage, uint64_t buffer, uint64_t offset = 0, uint64_t size = 0)
        : srcAccess_(srcAccess), dstAccess_(dstAccess), srcStage_(srcStage),
          dstStage_(dstStage), buffer_(buffer), offset_(offset), size_(size) {}

    void SetSrcAccess(uint16_t a) { srcAccess_ = a; }
    uint16_t SrcAccess() const { return srcAccess_; }
    void SetDstAccess(uint16_t a) { dstAccess_ = a; }
    uint16_t DstAccess() const { return dstAccess_; }
    void SetSrcStage(uint16_t s) { srcStage_ = s; }
    uint16_t SrcStage() const { return srcStage_; }
    void SetDstStage(uint16_t s) { dstStage_ = s; }
    uint16_t DstStage() const { return dstStage_; }
    void SetBuffer(uint64_t b) { buffer_ = b; }
    uint64_t Buffer() const { return buffer_; }
    void SetOffset(uint64_t o) { offset_ = o; }
    uint64_t Offset() const { return offset_; }
    void SetSize(uint64_t s) { size_ = s; }
    uint64_t Size() const { return size_; }

    bool IsWholeBuffer() const { return size_ == 0; }
    bool IsValid() const { return buffer_ != 0; }
    uint64_t EndOffset() const { return offset_ + (size_ ? size_ : 0); }

private:
    uint16_t srcAccess_ = 0, dstAccess_ = 0;
    uint16_t srcStage_ = 0, dstStage_ = 0;
    uint64_t buffer_ = 0, offset_ = 0, size_ = 0;
};

} // namespace bighero
