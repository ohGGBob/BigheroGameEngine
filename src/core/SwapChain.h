#pragma once
#include <cstdint>
#include "SurfaceFormat_v2.h"
#include "PresentMode_v2.h"

namespace bighero {

// SwapChain: describes a swapchain (backbuffer images for presentation).
// Self-contained, std-lib only.
class SwapChain {
public:
    SwapChain() = default;
    SwapChain(uint32_t minImageCount, SurfaceFormat format, PresentMode present)
        : minImageCount_(minImageCount), format_(format), present_(present) {}

    void SetMinImageCount(uint32_t c) { minImageCount_ = c; }
    uint32_t MinImageCount() const { return minImageCount_; }
    void SetImageCount(uint32_t c) { imageCount_ = c; }
    uint32_t ImageCount() const { return imageCount_; }
    void SetFormat(SurfaceFormat f) { format_ = f; }
    SurfaceFormat Format() const { return format_; }
    void SetPresentMode(PresentMode p) { present_ = p; }
    PresentMode Present() const { return present_; }

    void SetExtent(uint32_t w, uint32_t h) { width_ = w; height_ = h; }
    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }

    void SetPreTransform(uint32_t t) { preTransform_ = t; }
    uint32_t PreTransform() const { return preTransform_; }
    void SetCompositeAlpha(uint32_t a) { compositeAlpha_ = a; }
    uint32_t CompositeAlpha() const { return compositeAlpha_; }

    bool IsValid() const {
        return imageCount_ > 0 && width_ > 0 && height_ > 0 && format_.IsValid();
    }
    uint64_t BackbufferArea() const { return (uint64_t)width_ * height_; }

private:
    uint32_t minImageCount_ = 2;
    uint32_t imageCount_ = 0;
    SurfaceFormat format_;
    PresentMode present_;
    uint32_t width_ = 0, height_ = 0;
    uint32_t preTransform_ = 0, compositeAlpha_ = 0;
};

} // namespace bighero
