#pragma once
#include <cstdint>
#include <utility>
#include <vector>
#include <cstddef>

namespace bighero {

// Offscreen render target: a color + depth attachment abstraction with a
// viewport and clear state. CPU-side handle; real GPU ops are downstream.
class RenderTarget {
public:
    RenderTarget() {}
    RenderTarget(int width, int height, bool depth = true)
        : w_(width), h_(height), hasDepth_(depth) {}

    void Resize(int w, int h) { w_ = w; h_ = h; }
    int Width() const { return w_; }
    int Height() const { return h_; }
    void SetDepth(bool d) { hasDepth_ = d; }
    bool HasDepth() const { return hasDepth_; }

    // Clear color (normalized 0..1 RGBA).
    void SetClearColor(float r, float g, float b, float a = 1.0f) {
        clear_[0]=r; clear_[1]=g; clear_[2]=b; clear_[3]=a;
    }
    void GetClearColor(float& r, float& g, float& b, float& a) const {
        r=clear_[0]; g=clear_[1]; b=clear_[2]; a=clear_[3];
    }

    void SetColorTarget(uint64_t handle) { colorTarget_ = handle; }
    uint64_t ColorTarget() const { return colorTarget_; }
    void SetDepthTarget(uint64_t handle) { depthTarget_ = handle; }
    uint64_t DepthTarget() const { return depthTarget_; }

    bool IsSizeValid() const { return w_ > 0 && h_ > 0; }

private:
    int w_ = 0, h_ = 0;
    bool hasDepth_ = true;
    float clear_[4] = {0,0,0,1};
    uint64_t colorTarget_ = 0;
    uint64_t depthTarget_ = 0;
};

} // namespace bighero
