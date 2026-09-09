#pragma once
#include <cstdint>

namespace bighero {

// CopyImageInfo: describes an image-to-image copy region (src/dst image,
// offsets, extents). Self-contained, std-lib only.
class CopyImageInfo {
public:
    CopyImageInfo() = default;
    CopyImageInfo(uint64_t srcImage, uint64_t dstImage,
                  uint32_t srcX=0, uint32_t srcY=0, uint32_t srcZ=0,
                  uint32_t dstX=0, uint32_t dstY=0, uint32_t dstZ=0,
                  uint32_t width=0, uint32_t height=0, uint32_t depth=1)
        : srcImage_(srcImage), dstImage_(dstImage),
          srcX_(srcX), srcY_(srcY), srcZ_(srcZ),
          dstX_(dstX), dstY_(dstY), dstZ_(dstZ),
          width_(width), height_(height), depth_(depth) {}

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

    void SetSrcMip(uint32_t m) { srcMip_ = m; }
    uint32_t SrcMip() const { return srcMip_; }
    void SetDstMip(uint32_t m) { dstMip_ = m; }
    uint32_t DstMip() const { return dstMip_; }
    void SetLayerCount(uint32_t c) { layerCount_ = c; }
    uint32_t LayerCount() const { return layerCount_; }

    bool IsValid() const { return srcImage_ != 0 && dstImage_ != 0 && width_ > 0 && height_ > 0; }
    uint64_t PixelsCopied() const { return (uint64_t)width_ * height_ * depth_ * layerCount_; }

private:
    uint64_t srcImage_ = 0, dstImage_ = 0;
    uint32_t srcX_=0, srcY_=0, srcZ_=0, dstX_=0, dstY_=0, dstZ_=0;
    uint32_t width_=0, height_=0, depth_=1;
    uint32_t srcMip_=0, dstMip_=0, layerCount_=1;
};

} // namespace bighero
