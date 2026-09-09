#pragma once
#include <cstdint>

namespace bighero {

// BlendAttachment: per-attachment blend state for a graphics pipeline (color
// blend enable, source/destination factors, blend op, color write mask).
// Self-contained, std-lib only.
class BlendAttachment {
public:
    enum class Factor : uint8_t {
        Zero = 0, One = 1, SrcColor = 2, OneMinusSrcColor = 3,
        DstColor = 4, OneMinusDstColor = 5, SrcAlpha = 6,
        OneMinusSrcAlpha = 7, DstAlpha = 8, OneMinusDstAlpha = 9, Constant = 10
    };
    enum class Op : uint8_t { Add = 0, Subtract = 1, ReverseSubtract = 2, Min = 3, Max = 4 };

    BlendAttachment() = default;
    BlendAttachment(bool enable, Factor srcColor, Factor dstColor, Op op,
                    Factor srcAlpha, Factor dstAlpha, Op alphaOp)
        : enable_(enable), srcColor_(srcColor), dstColor_(dstColor), op_(op),
          srcAlpha_(srcAlpha), dstAlpha_(dstAlpha), alphaOp_(alphaOp) {}

    void Enable(bool b) { enable_ = b; }
    bool IsEnabled() const { return enable_; }
    void SetSrcColor(Factor f) { srcColor_ = f; }
    Factor SrcColor() const { return srcColor_; }
    void SetDstColor(Factor f) { dstColor_ = f; }
    Factor DstColor() const { return dstColor_; }
    void SetColorOp(Op o) { op_ = o; }
    Op ColorOp() const { return op_; }
    void SetSrcAlpha(Factor f) { srcAlpha_ = f; }
    Factor SrcAlpha() const { return srcAlpha_; }
    void SetDstAlpha(Factor f) { dstAlpha_ = f; }
    Factor DstAlpha() const { return dstAlpha_; }
    void SetAlphaOp(Op o) { alphaOp_ = o; }
    Op AlphaOp() const { return alphaOp_; }

    void SetWriteMask(uint8_t mask) { writeMask_ = mask; }
    uint8_t WriteMask() const { return writeMask_; }

    static const char* FactorName(Factor f) {
        switch (f) {
            case Factor::Zero: return "Zero";
            case Factor::One: return "One";
            case Factor::SrcAlpha: return "SrcAlpha";
            case Factor::OneMinusSrcAlpha: return "OneMinusSrcAlpha";
            default: return "Other";
        }
    }

private:
    bool enable_ = false;
    Factor srcColor_ = Factor::One, dstColor_ = Factor::Zero;
    Op op_ = Op::Add;
    Factor srcAlpha_ = Factor::One, dstAlpha_ = Factor::Zero;
    Op alphaOp_ = Op::Add;
    uint8_t writeMask_ = 0xF; // RGBA
};

} // namespace bighero
