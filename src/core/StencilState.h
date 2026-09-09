#pragma once
#include <cstdint>

namespace bighero {

// StencilState: descriptor for the stencil test/write configuration of a
// render pass. Holds test function, reference, mask, and per-face operations.
// Pure config container.
class StencilState {
public:
    enum class Func { Never, Less, LEqual, Equal, GEqual, Greater, NotEqual, Always };
    enum class Op { Keep, Zero, Replace, IncrWrap, DecrWrap, Invert };
    enum class Face { Front, Back };

    StencilState() {}

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetTest(Func f) { testFunc_ = f; }
    Func Test() const { return testFunc_; }
    void SetReference(std::uint8_t ref) { ref_ = ref; }
    std::uint8_t Reference() const { return ref_; }
    void SetMask(std::uint8_t m) { mask_ = m; }
    std::uint8_t Mask() const { return mask_; }
    void SetWriteMask(std::uint8_t m) { writeMask_ = m; }
    std::uint8_t WriteMask() const { return writeMask_; }
    void SetFailOp(Op o) { failOp_ = o; }
    Op FailOp() const { return failOp_; }
    void SetZFailOp(Op o) { zFailOp_ = o; }
    Op ZFailOp() const { return zFailOp_; }
    void SetPassOp(Op o) { passOp_ = o; }
    Op PassOp() const { return passOp_; }

    void SetFaceOp(Face face, Op onPass, Op onZFail, Op onFail) {
        if (face == Face::Front) { passOp_ = onPass; zFailOp_ = onZFail; failOp_ = onFail; }
        else { bPassOp_ = onPass; bZFailOp_ = onZFail; bFailOp_ = onFail; }
    }

    // Two-sided support.
    bool TwoSided() const { return twoSided_; }
    void SetTwoSided(bool t) { twoSided_ = t; }

private:
    bool enabled_ = false;
    bool twoSided_ = true;
    Func testFunc_ = Func::Always;
    std::uint8_t ref_ = 0;
    std::uint8_t mask_ = 0xFF;
    std::uint8_t writeMask_ = 0xFF;
    Op failOp_ = Op::Keep;
    Op zFailOp_ = Op::Keep;
    Op passOp_ = Op::Keep;
    Op bFailOp_ = Op::Keep;
    Op bZFailOp_ = Op::Keep;
    Op bPassOp_ = Op::Keep;
};

} // namespace bighero
