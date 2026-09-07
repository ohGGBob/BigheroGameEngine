#pragma once

namespace bighero {

// Depth/stencil state: depth compare func + write enable, plus stencil
// test/op configuration. CPU-side state payload for draw submissions.
class DepthStencilState {
public:
    enum class CompareFunc { Never, Less, LessEqual, Equal, NotEqual, GreaterEqual, Greater, Always };
    enum class StencilOp { Keep, Zero, Replace, Increment, Decrement, Invert };

    DepthStencilState() {}

    void SetDepthTest(bool t) { depthTest_ = t; }
    bool DepthTest() const { return depthTest_; }
    void SetDepthWrite(bool w) { depthWrite_ = w; }
    bool DepthWrite() const { return depthWrite_; }
    void SetDepthFunc(CompareFunc f) { depthFunc_ = f; }
    CompareFunc DepthFunc() const { return depthFunc_; }

    void SetStencilTest(bool t) { stencilTest_ = t; }
    bool StencilTest() const { return stencilTest_; }
    void SetStencilRef(int r) { stencilRef_ = r; }
    int StencilRef() const { return stencilRef_; }
    void SetStencilMask(int m) { stencilMask_ = m; }
    int StencilMask() const { return stencilMask_; }
    void SetStencilFunc(CompareFunc f) { stencilFunc_ = f; }
    CompareFunc StencilFunc() const { return stencilFunc_; }
    void SetStencilFail(StencilOp op) { stencilFail_ = op; }
    StencilOp StencilFail() const { return stencilFail_; }
    void SetStencilDepthFail(StencilOp op) { stencilDepthFail_ = op; }
    StencilOp StencilDepthFail() const { return stencilDepthFail_; }
    void SetStencilPass(StencilOp op) { stencilPass_ = op; }
    StencilOp StencilPass() const { return stencilPass_; }

    static DepthStencilState Default() {
        DepthStencilState d;
        d.depthTest_ = true; d.depthWrite_ = true; d.depthFunc_ = CompareFunc::LessEqual;
        return d;
    }
    static DepthStencilState DepthReadOnly() {
        DepthStencilState d;
        d.depthTest_ = true; d.depthWrite_ = false; d.depthFunc_ = CompareFunc::LessEqual;
        return d;
    }
    static DepthStencilState NoDepth() {
        DepthStencilState d;
        d.depthTest_ = false; d.depthWrite_ = false;
        return d;
    }

private:
    bool depthTest_ = true;
    bool depthWrite_ = true;
    CompareFunc depthFunc_ = CompareFunc::LessEqual;
    bool stencilTest_ = false;
    int stencilRef_ = 0;
    int stencilMask_ = 0xFF;
    CompareFunc stencilFunc_ = CompareFunc::Always;
    StencilOp stencilFail_ = StencilOp::Keep;
    StencilOp stencilDepthFail_ = StencilOp::Keep;
    StencilOp stencilPass_ = StencilOp::Keep;
};

} // namespace bighero
