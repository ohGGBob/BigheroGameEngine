#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "UniformBlock_v2.h"

namespace bighero {

// UniformBuffer: a GPU-side uniform buffer storage acting as a container of
// uniform blocks bound to a binding point. Self-contained.
class UniformBuffer {
public:
    UniformBuffer() = default;
    UniformBuffer(uint32_t binding, uint32_t size)
        : binding_(binding), size_(size) {}

    void SetBinding(uint32_t b) { binding_ = b; }
    uint32_t Binding() const { return binding_; }
    void SetSize(uint32_t s) { size_ = s; }
    uint32_t Size() const { return size_; }

    // Add a uniform block and return its index.
    int AddBlock(const UniformBlock& block) {
        blocks_.push_back(block);
        return (int)blocks_.size() - 1;
    }
    size_t BlockCount() const { return blocks_.size(); }
    UniformBlock& BlockAt(size_t i) { return blocks_[i]; }
    const UniformBlock& BlockAt(size_t i) const { return blocks_[i]; }
    void RemoveAll() { blocks_.clear(); }

    uint64_t TotalBytes() const {
        uint64_t total = 0;
        for (auto& b : blocks_) total += b.Size();
        return total;
    }

private:
    uint32_t binding_ = 0;
    uint32_t size_ = 0;
    std::vector<UniformBlock> blocks_;
};

} // namespace bighero
