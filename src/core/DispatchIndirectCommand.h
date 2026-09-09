#pragma once
#include <cstdint>

namespace bighero {

// DispatchIndirectCommand: a single record for an indirect compute dispatch.
// Self-contained, std-lib only.
class DispatchIndirectCommand {
public:
    DispatchIndirectCommand() = default;
    DispatchIndirectCommand(uint32_t x, uint32_t y, uint32_t z)
        : groupCountX_(x), groupCountY_(y), groupCountZ_(z) {}

    void SetGroupCountX(uint32_t x) { groupCountX_ = x; }
    uint32_t GroupCountX() const { return groupCountX_; }
    void SetGroupCountY(uint32_t y) { groupCountY_ = y; }
    uint32_t GroupCountY() const { return groupCountY_; }
    void SetGroupCountZ(uint32_t z) { groupCountZ_ = z; }
    uint32_t GroupCountZ() const { return groupCountZ_; }

    void SetGroupCount(uint32_t x, uint32_t y, uint32_t z) {
        groupCountX_ = x; groupCountY_ = y; groupCountZ_ = z;
    }
    uint64_t TotalGroups() const {
        return (uint64_t)groupCountX_ * (uint64_t)groupCountY_ * (uint64_t)groupCountZ_;
    }
    bool IsValid() const { return groupCountX_ > 0 && groupCountY_ > 0 && groupCountZ_ > 0; }

    void Pack(uint32_t out[3]) const {
        out[0] = groupCountX_; out[1] = groupCountY_; out[2] = groupCountZ_;
    }
    static void Unpack(const uint32_t in[3], DispatchIndirectCommand& cmd) {
        cmd.groupCountX_ = in[0]; cmd.groupCountY_ = in[1]; cmd.groupCountZ_ = in[2];
    }

private:
    uint32_t groupCountX_ = 1;
    uint32_t groupCountY_ = 1;
    uint32_t groupCountZ_ = 1;
};

} // namespace bighero
