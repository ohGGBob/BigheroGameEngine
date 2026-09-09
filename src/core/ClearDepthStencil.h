#pragma once
#include <cstdint>

namespace bighero {

// ClearDepthStencil: depth/stencil values used to clear a depth-stencil
// attachment. Self-contained, std-lib only.
class ClearDepthStencil {
public:
    ClearDepthStencil() = default;
    ClearDepthStencil(float depth, uint32_t stencil = 0)
        : depth_(depth), stencil_(stencil) {}

    void SetDepth(float d) { depth_ = d; }
    float Depth() const { return depth_; }
    void SetStencil(uint32_t s) { stencil_ = s; }
    uint32_t Stencil() const { return stencil_; }
    void Set(float d, uint32_t s = 0) { depth_ = d; stencil_ = s; }

    static ClearDepthStencil Default() { return ClearDepthStencil(1.0f, 0); }
    static ClearDepthStencil Zero() { return ClearDepthStencil(0.0f, 0); }
    static ClearDepthStencil One() { return ClearDepthStencil(1.0f, 0); }

    void Pack(float out[2]) const {
        out[0] = depth_;
        out[1] = static_cast<float>(stencil_);
    }

private:
    float depth_ = 1.0f;
    uint32_t stencil_ = 0;
};

} // namespace bighero
