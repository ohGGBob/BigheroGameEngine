#pragma once
#include <cstdint>

namespace bighero {

// StencilOp: stencil test operations used in depth/stencil state.
// Self-contained, std-lib only.
class StencilOp {
public:
    enum class Op : uint8_t { Keep=0, Zero=1, Replace=2, IncrementClamp=3, DecrementClamp=4, Invert=5, IncrementWrap=6, DecrementWrap=7 };

    static bool IsValid(Op o) { return o<=Op::DecrementWrap; }
    static bool IsKeep(Op o) { return o==Op::Keep; }
    static const char* Name(Op o) {
        switch (o) {
            case Op::Keep: return "Keep"; case Op::Zero: return "Zero"; case Op::Replace: return "Replace";
            case Op::IncrementClamp: return "IncrementClamp"; case Op::DecrementClamp: return "DecrementClamp";
            case Op::Invert: return "Invert"; case Op::IncrementWrap: return "IncrementWrap"; case Op::DecrementWrap: return "DecrementWrap";
        }
        return "Unknown";
    }
};

} // namespace bighero
