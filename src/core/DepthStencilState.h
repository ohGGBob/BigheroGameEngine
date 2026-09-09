#pragma once
#include <cstdint>

namespace bighero {

// DepthStencilState: configures depth and stencil testing for the graphics
// pipeline. Self-contained, std-lib only.
class DepthStencilState {
public:
    enum class CompareOp : uint8_t { Never=0, Less=1, Equal=2, LessEqual=3, Greater=4, NotEqual=5, GreaterEqual=6, Always=7 };
    enum class StencilOp : uint8_t { Keep=0, Zero=1, Replace=2, Increment=3, Decrement=4, Invert=5 };

    void SetDepthTest(bool b) { depthTest_ = b; }
    bool DepthTest() const { return depthTest_; }
    void SetDepthWrite(bool b) { depthWrite_ = b; }
    bool DepthWrite() const { return depthWrite_; }
    void SetDepthCompare(CompareOp op) { depthCompare_ = op; }
    CompareOp DepthCompare() const { return depthCompare_; }
    void SetDepthBias(bool b) { depthBiasEnabled_ = b; }
    bool DepthBias() const { return depthBiasEnabled_; }

    void SetStencilTest(bool b) { stencilTest_ = b; }
    bool StencilTest() const { return stencilTest_; }
    void SetStencilCompareMask(uint32_t m) { stencilReadMask_ = m; }
    uint32_t StencilCompareMask() const { return stencilReadMask_; }
    void SetStencilWriteMask(uint32_t m) { stencilWriteMask_ = m; }
    uint32_t StencilWriteMask() const { return stencilWriteMask_; }
    void SetStencilFail(StencilOp op) { stencilFailOp_ = op; }
    StencilOp StencilFail() const { return stencilFailOp_; }
    void SetStencilPass(StencilOp op) { stencilPassOp_ = op; }
    StencilOp StencilPass() const { return stencilPassOp_; }

    void SetMinDepth(float d) { minDepth_ = d; }
    void SetMaxDepth(float d) { maxDepth_ = d; }
    float MinDepth() const { return minDepth_; }
    float MaxDepth() const { return maxDepth_; }

    bool IsDepthEnabled() const { return depthTest_; }
    static const char* CompareOpName(CompareOp op) {
        switch (op) { case CompareOp::Less: return "Less"; case CompareOp::Always: return "Always"; default: return "Compare"; }
    }

private:
    bool depthTest_ = true, depthWrite_ = true, depthBiasEnabled_ = false;
    CompareOp depthCompare_ = CompareOp::Less;
    bool stencilTest_ = false, stencilWriteEnabled_ = false;
    uint32_t stencilReadMask_ = 0xFFFFFFFFu, stencilWriteMask_ = 0xFFFFFFFFu;
    StencilOp stencilFailOp_ = StencilOp::Keep, stencilPassOp_ = StencilOp::Keep;
    float minDepth_ = 0.0f, maxDepth_ = 1.0f;
};

} // namespace bighero
