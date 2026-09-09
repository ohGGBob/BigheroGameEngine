#pragma once
#include <cstdint>

namespace bighero {

// MemoryBarrier: a global synchronization barrier (src/dst access and stage
// masks) applying to all memory accesses. Self-contained, std-lib only.
class MemoryBarrier {
public:
    MemoryBarrier() = default;
    MemoryBarrier(uint16_t srcAccess, uint16_t dstAccess, uint16_t srcStage, uint16_t dstStage)
        : srcAccess_(srcAccess), dstAccess_(dstAccess), srcStage_(srcStage), dstStage_(dstStage) {}

    void SetSrcAccess(uint16_t a) { srcAccess_ = a; }
    uint16_t SrcAccess() const { return srcAccess_; }
    void SetDstAccess(uint16_t a) { dstAccess_ = a; }
    uint16_t DstAccess() const { return dstAccess_; }
    void SetSrcStage(uint16_t s) { srcStage_ = s; }
    uint16_t SrcStage() const { return srcStage_; }
    void SetDstStage(uint16_t s) { dstStage_ = s; }
    uint16_t DstStage() const { return dstStage_; }

    void SetDependencyFlags(uint32_t f) { flags_ = f; }
    uint32_t DependencyFlags() const { return flags_; }

    bool IsValid() const { return srcStage_ != 0 || dstStage_ != 0; }
    static const char* AccessName(uint16_t a) {
        switch (a) {
            case 0: return "None";
            case 1: return "ShaderRead";
            case 2: return "ShaderWrite";
            case 4: return "ColorWrite";
            case 8: return "DepthWrite";
            default: return "Mixed";
        }
    }

private:
    uint16_t srcAccess_ = 0, dstAccess_ = 0;
    uint16_t srcStage_ = 0, dstStage_ = 0;
    uint32_t flags_ = 0;
};

} // namespace bighero
