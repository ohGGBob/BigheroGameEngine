#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace bighero {

// ComputeBuffer: a GPU-memory buffer descriptor used to hold arbitrary data
// (positions, velocities, counters) for compute shader reads/writes. Tracks
// element count, stride, and a usage/access flag. Pure config container.
class ComputeBuffer {
public:
    enum class Access { ReadOnly, WriteOnly, ReadWrite };
    enum class Type { Structured, Raw, Counter };

    ComputeBuffer() {}
    ComputeBuffer(std::size_t count, std::size_t stride)
        : count_(count), stride_(stride) {}

    void Allocate(std::size_t count, std::size_t stride) {
        count_ = count; stride_ = stride;
    }
    std::size_t Count() const { return count_; }
    std::size_t Stride() const { return stride_; }
    std::size_t TotalBytes() const { return count_ * stride_; }

    void SetAccess(Access a) { access_ = a; }
    Access AccessMode() const { return access_; }
    void SetType(Type t) { type_ = t; }
    Type BufferType() const { return type_; }

    void SetBindingSlot(int slot) { slot_ = slot; }
    int BindingSlot() const { return slot_; }
    void SetHandle(std::uint64_t h) { handle_ = h; }
    std::uint64_t Handle() const { return handle_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }

    void Release() { handle_ = 0; count_ = 0; stride_ = 0; }
    bool IsAllocated() const { return handle_ != 0 && count_ > 0; }

private:
    std::size_t count_ = 0;
    std::size_t stride_ = 0;
    Access access_ = Access::ReadWrite;
    Type type_ = Type::Structured;
    int slot_ = 0;
    std::uint64_t handle_ = 0;
    bool enabled_ = true;
};

} // namespace bighero
