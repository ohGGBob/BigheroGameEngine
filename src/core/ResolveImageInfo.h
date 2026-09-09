#pragma once
#include <cstdint>

namespace bighero {

// ResolveImageInfo: describes a multisample resolve operation (resolve src
// image to dst image). Self-contained, std-lib only.
class ResolveImageInfo {
public:
    ResolveImageInfo() = default;
    ResolveImageInfo(uint64_t srcImage, uint64_t dstImage)
        : srcImage_(srcImage), dstImage_(dstImage) {}

    void SetSrcImage(uint64_t i) { srcImage_ = i; }
    uint64_t SrcImage() const { return srcImage_; }
    void SetDstImage(uint64_t i) { dstImage_ = i; }
    uint64_t DstImage() const { return dstImage_; }

    void SetSrcOffset(uint32_t x, uint32_t y, uint32_t z = 0) { srcX_=x; srcY_=y; srcZ_=z; }
    uint32_t SrcX() const { return srcX_; }
    uint32_t SrcY() const { return srcY_; }
    uint32_t SrcZ() const { return srcZ_; }

    void SetDstOffset(uint32_t x, uint32_t y, uint32_t z = 0) { dstX_=x; dstY_=y; dstZ_=z; }
    uint32_t DstX() const { return dstX_; }
    uint32_t DstY() const { return dstY_; }
    uint32_t DstZ() const { return dstZ_; }

    void SetExtent(uint32_t w, uint32_t h, uint32_t d = 1) { width_=w; height_=h; depth_=d; }
    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    uint32_t Depth() const { return depth_; }

    void SetLayerCount(uint32_t c) { layerCount_ = c; }
    uint32_t LayerCount() const { return layerCount_; }

    bool IsValid() const { return srcImage_ != 0 && dstImage_ != 0 && width_ > 0 && height_ > 0; }
    uint64_t PixelsResolved() const { return (uint64_t)width_ * height_ * depth_ * layerCount_; }
    static const char* OpName() { return "Resolve"; }

private:
    uint64_t srcImage_ = 0, dstImage_ = 0;
    uint32_t srcX_=0, srcY_=0, srcZ_=0, dstX_=0, dstY_=0, dstZ_=0;
    uint32_t width_=0, height_=0, depth_=1, layerCount_=1;
};

} // namespace bighero
