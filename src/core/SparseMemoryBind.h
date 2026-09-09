#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// SparseMemoryBind: describes a sparse memory binding (offset + resource
// range + memory handle) for partially resident resources. Self-contained.
class SparseMemoryBind {
public:
    SparseMemoryBind() = default;
    SparseMemoryBind(uint64_t resourceOffset, uint64_t resourceSize, uint64_t memory, uint64_t memoryOffset = 0)
        : resourceOffset_(resourceOffset), resourceSize_(resourceSize),
          memory_(memory), memoryOffset_(memoryOffset) {}

    void SetResourceOffset(uint64_t o) { resourceOffset_ = o; }
    uint64_t ResourceOffset() const { return resourceOffset_; }
    void SetResourceSize(uint64_t s) { resourceSize_ = s; }
    uint64_t ResourceSize() const { return resourceSize_; }
    void SetMemory(uint64_t m) { memory_ = m; }
    uint64_t Memory() const { return memory_; }
    void SetMemoryOffset(uint64_t o) { memoryOffset_ = o; }
    uint64_t MemoryOffset() const { return memoryOffset_; }

    void SetFlags(uint32_t f) { flags_ = f; }
    uint32_t Flags() const { return flags_; }
    void AddFlag(uint32_t f) { flags_ |= f; }
    void RemoveFlag(uint32_t f) { flags_ &= ~f; }
    bool HasFlag(uint32_t f) const { return (flags_ & f) != 0; }

    bool IsBound() const { return memory_ != 0; }
    uint64_t EndResourceOffset() const { return resourceOffset_ + resourceSize_; }
    bool Overlaps(const SparseMemoryBind& o) const {
        return resourceOffset_ < o.EndResourceOffset() && o.resourceOffset_ < EndResourceOffset();
    }
    bool IsValid() const { return resourceSize_ > 0; }

private:
    uint64_t resourceOffset_ = 0, resourceSize_ = 0;
    uint64_t memory_ = 0, memoryOffset_ = 0;
    uint32_t flags_ = 0;
};

} // namespace bighero
