#pragma once
#include <cstdint>

namespace bighero {

// BufferView: a typed view into a buffer (offset, size, stride, usage).
// Self-contained, std-lib only.
class BufferView {
public:
    BufferView() = default;
    BufferView(uint64_t buffer, uint64_t offset, uint64_t size, uint32_t stride = 0)
        : buffer_(buffer), offset_(offset), size_(size), stride_(stride) {}

    void SetBuffer(uint64_t b) { buffer_ = b; }
    uint64_t Buffer() const { return buffer_; }
    void SetOffset(uint64_t o) { offset_ = o; }
    uint64_t Offset() const { return offset_; }
    void SetSize(uint64_t s) { size_ = s; }
    uint64_t Size() const { return size_; }
    void SetStride(uint32_t s) { stride_ = s; }
    uint32_t Stride() const { return stride_; }
    void SetByteLength(uint32_t l) { byteLength_ = l; }
    uint32_t ByteLength() const { return byteLength_; }

    bool IsValid() const { return buffer_ != 0; }
    uint64_t EndOffset() const { return offset_ + size_; }
    uint64_t ElementCount() const { return stride_ ? size_/stride_ : 0; }
    bool IsNull() const { return buffer_ == 0; }

private:
    uint64_t buffer_ = 0, offset_ = 0, size_ = 0;
    uint32_t stride_ = 0, byteLength_ = 0;
};

} // namespace bighero
