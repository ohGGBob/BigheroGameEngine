#pragma once
#include <cstdint>
#include "DispatchIndirectCommand.h"

namespace bighero {

// ComputeDispatchInfo: describes a compute dispatch (group counts, possibly
// indirect via a command buffer). Self-contained, std-lib only.
class ComputeDispatchInfo {
public:
    ComputeDispatchInfo() = default;
    ComputeDispatchInfo(uint32_t x, uint32_t y, uint32_t z)
        : groupCountX_(x), groupCountY_(y), groupCountZ_(z) {}

    void SetGroupCount(uint32_t x, uint32_t y, uint32_t z) {
        groupCountX_=x; groupCountY_=y; groupCountZ_=z;
    }
    uint32_t GroupCountX() const { return groupCountX_; }
    uint32_t GroupCountY() const { return groupCountY_; }
    uint32_t GroupCountZ() const { return groupCountZ_; }

    uint64_t TotalGroups() const {
        return (uint64_t)groupCountX_ * groupCountY_ * groupCountZ_;
    }
    bool IsValid() const { return groupCountX_ > 0 && groupCountY_ > 0 && groupCountZ_ > 0; }

    void SetIndirect(bool b) { indirect_ = b; }
    bool IsIndirect() const { return indirect_; }
    void SetIndirectBuffer(uint64_t buf) { indirectBuffer_ = buf; indirect_ = true; }
    uint64_t IndirectBuffer() const { return indirectBuffer_; }

    void ToCommand(DispatchIndirectCommand& cmd) const {
        cmd.SetGroupCount(groupCountX_, groupCountY_, groupCountZ_);
    }

private:
    uint32_t groupCountX_ = 1, groupCountY_ = 1, groupCountZ_ = 1;
    bool indirect_ = false;
    uint64_t indirectBuffer_ = 0;
};

} // namespace bighero
