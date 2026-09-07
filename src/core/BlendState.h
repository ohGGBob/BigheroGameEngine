#pragma once
#include <cstdint>

namespace bighero {

// Blend state describing how source and destination colors combine per
// channel. Mirrors common graphics API blend factors.
class BlendState {
public:
    enum class Factor {
        Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor,
        SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha,
        SrcAlphaSaturate
    };
    enum class Equation { Add, Subtract, ReverseSubtract, Min, Max };

    BlendState() {}
    BlendState(Factor src, Factor dst, Equation eq = Equation::Add)
        : src_(src), dst_(dst), eq_(eq) {}

    void SetSrc(Factor f) { src_ = f; }
    Factor Src() const { return src_; }
    void SetDst(Factor f) { dst_ = f; }
    Factor Dst() const { return dst_; }
    void SetEquation(Equation e) { eq_ = e; }
    Equation Eq() const { return eq_; }

    void SetBlend(bool enabled) { blend_ = enabled; }
    bool Blend() const { return blend_; }
    void SetDepthWrite(bool d) { depthWrite_ = d; }
    bool DepthWrite() const { return depthWrite_; }
    void SetDepthTest(bool d) { depthTest_ = d; }
    bool DepthTest() const { return depthTest_; }

    // Convenience presets.
    static BlendState Opaque() {
        BlendState b(Factor::One, Factor::Zero);
        b.SetBlend(false); b.SetDepthWrite(true); b.SetDepthTest(true);
        return b;
    }
    static BlendState AlphaBlend() {
        BlendState b(Factor::SrcAlpha, Factor::OneMinusSrcAlpha);
        b.SetBlend(true); b.SetDepthWrite(false); b.SetDepthTest(true);
        return b;
    }
    static BlendState Additive() {
        BlendState b(Factor::SrcAlpha, Factor::One);
        b.SetBlend(true); b.SetDepthWrite(false); b.SetDepthTest(true);
        return b;
    }

    bool IsTransparent() const { return blend_; }

private:
    Factor src_ = Factor::One, dst_ = Factor::Zero;
    Equation eq_ = Equation::Add;
    bool blend_ = false;
    bool depthWrite_ = true;
    bool depthTest_ = true;
};

} // namespace bighero
