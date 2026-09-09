#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// IndirectCommandBuffer: a buffer that holds a batch of indirect commands
// (draw, draw-indexed, dispatch) for multi-draw-indirect. Self-contained.
class IndirectCommandBuffer {
public:
    enum class CommandType : uint8_t { Draw = 0, DrawIndexed = 1, Dispatch = 2 };

    IndirectCommandBuffer() = default;
    IndirectCommandBuffer(uint32_t capacity, CommandType type)
        : capacity_(capacity), type_(type) {
        buffer_.assign(capacity_ * Stride(), 0);
    }

    void Allocate(uint32_t capacity, CommandType type) {
        capacity_ = capacity; type_ = type;
        buffer_.assign(capacity_ * Stride(), 0);
    }
    CommandType Type() const { return type_; }
    uint32_t Capacity() const { return capacity_; }
    uint32_t Count() const { return count_; }
    bool IsEmpty() const { return count_ == 0; }

    uint32_t Stride() const {
        return type_ == CommandType::Dispatch ? 12 : 20;
    }
    const std::vector<uint8_t>& Buffer() const { return buffer_; }
    uint8_t* Data() { return buffer_.data(); }
    const uint8_t* Data() const { return buffer_.data(); }

    // Record a draw command at the given index.
    void RecordDraw(uint32_t cmd, uint32_t vertexCount, uint32_t instanceCount,
                    uint32_t firstVertex, uint32_t firstInstance) {
        if (cmd >= capacity_ || type_ != CommandType::Draw) return;
        uint32_t off = cmd * Stride();
        WriteU32(off+0, vertexCount); WriteU32(off+4, instanceCount);
        WriteU32(off+8, firstVertex); WriteU32(off+12, firstInstance);
        if (count_ <= cmd) count_ = cmd + 1;
    }
    void RecordDrawIndexed(uint32_t cmd, uint32_t indexCount, uint32_t instanceCount,
                           uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
        if (cmd >= capacity_ || type_ != CommandType::DrawIndexed) return;
        uint32_t off = cmd * Stride();
        WriteU32(off+0, indexCount); WriteU32(off+4, instanceCount);
        WriteU32(off+8, firstIndex); WriteU32(off+12, (uint32_t)vertexOffset);
        WriteU32(off+16, firstInstance);
        if (count_ <= cmd) count_ = cmd + 1;
    }

    void Reset() { count_ = 0; }

private:
    void WriteU32(uint32_t off, uint32_t v) {
        if (off + 4 > buffer_.size()) return;
        buffer_[off]=(v>>0)&0xFF; buffer_[off+1]=(v>>8)&0xFF;
        buffer_[off+2]=(v>>16)&0xFF; buffer_[off+3]=(v>>24)&0xFF;
    }
    uint32_t capacity_ = 0;
    uint32_t count_ = 0;
    CommandType type_ = CommandType::Draw;
    std::vector<uint8_t> buffer_;
};

} // namespace bighero
