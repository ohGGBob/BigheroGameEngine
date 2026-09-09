#pragma once
#include <cstdint>

namespace bighero {

// CopyBufferInfo: describes a buffer-to-buffer copy region (src/dst offsets
// and byte length). Self-contained, std-lib only.
class CopyBufferInfo {
public:
    CopyBufferInfo() = default;
    CopyBufferInfo(uint64_t srcBuffer, uint64_t dstBuffer,
                   uint64_t srcOffset = 0, uint64_t dstOffset = 0, uint64_t size = 0)
        : srcBuffer_(srcBuffer), dstBuffer_(dstBuffer),
          srcOffset_(srcOffset), dstOffset_(dstOffset), size_(size) {}

    void SetSrcBuffer(uint64_t b) { srcBuffer_ = b; }
    uint64_t SrcBuffer() const { return srcBuffer_; }
    void SetDstBuffer(uint64_t b) { dstBuffer_ = b; }
    uint64_t DstBuffer() const { return dstBuffer_; }
    void SetSrcOffset(uint64_t o) { srcOffset_ = o; }
    uint64_t SrcOffset() const { return srcOffset_; }
    void SetDstOffset(uint64_t o) { dstOffset_ = o; }
    uint64_t DstOffset() const { return dstOffset_; }
    void SetSize(uint64_t s) { size_ = s; }
    uint64_t Size() const { return size_; }

    void SetSrcStructSize(uint64_t s) { srcStruct_ = s; }
    uint64_t SrcStructSize() const { return srcStruct_; }
    void SetDstStructSize(uint64_t s) { dstStruct_ = s; }
    uint64_t DstStructSize() const { return dstStruct_; }

    bool IsValid() const { return srcBuffer_ != 0 && dstBuffer_ != 0 && size_ > 0; }
    uint64_t CopiedBytes() const { return size_; }

private:
    uint64_t srcBuffer_ = 0, dstBuffer_ = 0;
    uint64_t srcOffset_ = 0, dstOffset_ = 0, size_ = 0;
    uint64_t srcStruct_ = 0, dstStruct_ = 0;
};

} // namespace bighero
