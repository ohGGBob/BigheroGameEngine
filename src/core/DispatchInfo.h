#pragma once
#include <cstddef>

namespace bighero {

// DispatchInfo: the parameters for a single compute dispatch — thread group
// counts and an optional shared memory size hint. This is the CPU-side record
// the backend submits to the command stream.
class DispatchInfo {
public:
    DispatchInfo() {}
    DispatchInfo(std::size_t groupX, std::size_t groupY, std::size_t groupZ)
        : groupX_(groupX), groupY_(groupY), groupZ_(groupZ) {}

    void SetGroupCounts(std::size_t x, std::size_t y, std::size_t z) {
        groupX_ = x < 1 ? 1 : x; groupY_ = y < 1 ? 1 : y; groupZ_ = z < 1 ? 1 : z;
    }
    std::size_t GroupX() const { return groupX_; }
    std::size_t GroupY() const { return groupY_; }
    std::size_t GroupZ() const { return groupZ_; }

    void SetSharedMemoryBytes(std::size_t c) { sharedMem_ = c; }
    std::size_t SharedMemoryBytes() const { return sharedMem_; }

    void SetShaderId(std::size_t id) { shaderId_ = id; }
    std::size_t ShaderId() const { return shaderId_; }

    // Total thread groups dispatched.
    std::size_t TotalGroups() const { return groupX_ * groupY_ * groupZ_; }
    bool IsValid() const { return groupX_ > 0 && groupY_ > 0 && groupZ_ > 0; }

    void Reset() { groupX_ = groupY_ = groupZ_ = 1; sharedMem_ = 0; shaderId_ = 0; }

private:
    std::size_t groupX_ = 1, groupY_ = 1, groupZ_ = 1;
    std::size_t sharedMem_ = 0;
    std::size_t shaderId_ = 0;
};

} // namespace bighero
