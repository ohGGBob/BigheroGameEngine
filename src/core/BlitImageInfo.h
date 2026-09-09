#pragma once
#include <cstdint>

namespace bighero {

// BlitImageInfo: describes an image blit (stretch copy with filtering) between
// an src and dst image. Self-contained, std-lib only.
class BlitImageInfo {
public:
    enum class Filter : uint8_t { Nearest = 0, Linear = 1, Cubic = 2 };

    BlitImageInfo() = default;
    BlitImageInfo(uint64_t srcImage, uint64_t dstImage, Filter filter)
        : srcImage_(srcImage), dstImage_(dstImage), filter_(filter) {}

    void SetSrcImage(uint64_t i) { srcImage_ = i; }
    uint64_t SrcImage() const { return srcImage_; }
    void SetDstImage(uint64_t i) { dstImage_ = i; }
    uint64_t DstImage() const { return dstImage_; }
    void SetFilter(Filter f) { filter_ = f; }
    Filter GetFilter() const { return filter_; }

    void SetSrcRect(int32_t x, int32_t y, int32_t z, uint32_t w, uint32_t h, uint32_t d = 1) {
        srcX_=x; srcY_=y; srcZ_=z; srcW_=w; srcH_=h; srcD_=d;
    }
    void SetDstRect(int32_t x, int32_t y, int32_t z, uint32_t w, uint32_t h, uint32_t d = 1) {
        dstX_=x; dstY_=y; dstZ_=z; dstW_=w; dstH_=h; dstD_=d;
    }
    int32_t SrcX() const { return srcX_; }
    int32_t DstX() const { return dstX_; }
    uint32_t SrcWidth() const { return srcW_; }
    uint32_t DstWidth() const { return dstW_; }

    void SetSrcMip(uint32_t m) { srcMip_ = m; }
    uint32_t SrcMip() const { return srcMip_; }
    void SetDstMip(uint32_t m) { dstMip_ = m; }
    uint32_t DstMip() const { return dstMip_; }

    bool IsValid() const { return srcImage_ != 0 && dstImage_ != 0; }
    bool IsFullSize() const { return srcW_ == dstW_ && srcH_ == dstH_ && srcD_ == dstD_; }
    static const char* FilterName(Filter f) {
        switch (f) { case Filter::Nearest: return "Nearest"; case Filter::Linear: return "Linear"; case Filter::Cubic: return "Cubic"; }
        return "Unknown";
    }

private:
    uint64_t srcImage_ = 0, dstImage_ = 0;
    Filter filter_ = Filter::Nearest;
    int32_t srcX_=0, srcY_=0, srcZ_=0, dstX_=0, dstY_=0, dstZ_=0;
    uint32_t srcW_=0, srcH_=0, srcD_=1, dstW_=0, dstH_=0, dstD_=1;
    uint32_t srcMip_=0, dstMip_=0;
};

} // namespace bighero
