#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// DescriptorPool: a pool that allocates descriptor sets from a fixed pool.
// Self-contained, std-lib only.
class DescriptorPool {
public:
    struct PoolSize {
        uint16_t type = 0;    // 0=uniform buffer, 1=combined sampler, 2=storage buffer, 3=texture
        uint32_t count = 0;
    };

    DescriptorPool() = default;
    explicit DescriptorPool(uint32_t maxSets) : maxSets_(maxSets) {}

    void SetMaxSets(uint32_t m) { maxSets_ = m; }
    uint32_t MaxSets() const { return maxSets_; }

    void AddPoolSize(uint16_t type, uint32_t count) {
        PoolSize ps; ps.type = type; ps.count = count;
        poolSizes_.push_back(ps);
        totalDescriptors_ += count;
    }
    size_t PoolSizeCount() const { return poolSizes_.size(); }
    const PoolSize& PoolSizeAt(size_t i) const { return poolSizes_[i]; }
    uint32_t TotalDescriptors() const { return totalDescriptors_; }

    bool AllocateSet(uint32_t count = 1) {
        if (allocatedSets_ + count > maxSets_) return false;
        allocatedSets_ += count;
        return true;
    }
    void FreeSet(uint32_t count = 1) {
        allocatedSets_ = allocatedSets_ > count ? allocatedSets_ - count : 0;
    }
    void Reset() { allocatedSets_ = 0; }
    uint32_t AllocatedSets() const { return allocatedSets_; }
    uint32_t AvailableSets() const { return maxSets_ > allocatedSets_ ? maxSets_ - allocatedSets_ : 0; }

private:
    uint32_t maxSets_ = 0;
    uint32_t allocatedSets_ = 0;
    uint32_t totalDescriptors_ = 0;
    std::vector<PoolSize> poolSizes_;
};

} // namespace bighero
