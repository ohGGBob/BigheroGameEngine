#pragma once
#include <cstdint>
#include "ImageLayout.h"

namespace bighero {

// ImageMemoryBarrier: a synchronization barrier for a device image (old/new
// layouts, src/dst access, subresource range). Self-contained.
class ImageMemoryBarrier {
public:
    ImageMemoryBarrier() = default;
    ImageMemoryBarrier(uint16_t srcAccess, uint16_t dstAccess, ImageLayout::Layout oldLayout,
                       ImageLayout::Layout newLayout, uint64_t image)
        : srcAccess_(srcAccess), dstAccess_(dstAccess), oldLayout_(oldLayout),
          newLayout_(newLayout), image_(image) {}

    void SetSrcAccess(uint16_t a) { srcAccess_ = a; }
    uint16_t SrcAccess() const { return srcAccess_; }
    void SetDstAccess(uint16_t a) { dstAccess_ = a; }
    uint16_t DstAccess() const { return dstAccess_; }
    void SetOldLayout(ImageLayout::Layout l) { oldLayout_ = l; }
    ImageLayout::Layout OldLayout() const { return oldLayout_; }
    void SetNewLayout(ImageLayout::Layout l) { newLayout_ = l; }
    ImageLayout::Layout NewLayout() const { return newLayout_; }
    void SetImage(uint64_t i) { image_ = i; }
    uint64_t Image() const { return image_; }

    void SetSubresourceRange(uint32_t baseMip, uint32_t mipCount, uint32_t baseLayer, uint32_t layerCount) {
        baseMip_ = baseMip; mipCount_ = mipCount; baseLayer_ = baseLayer; layerCount_ = layerCount;
    }
    uint32_t BaseMip() const { return baseMip_; }
    uint32_t MipCount() const { return mipCount_; }
    uint32_t BaseLayer() const { return baseLayer_; }
    uint32_t LayerCount() const { return layerCount_; }

    void SetSrcStage(uint16_t s) { srcStage_ = s; }
    uint16_t SrcStage() const { return srcStage_; }
    void SetDstStage(uint16_t s) { dstStage_ = s; }
    uint16_t DstStage() const { return dstStage_; }

    bool IsValid() const { return image_ != 0; }
    bool IsLayoutTransition() const { return oldLayout_ != newLayout_; }

private:
    uint16_t srcAccess_ = 0, dstAccess_ = 0;
    ImageLayout::Layout oldLayout_ = ImageLayout::Layout::Undefined;
    ImageLayout::Layout newLayout_ = ImageLayout::Layout::Undefined;
    uint64_t image_ = 0;
    uint32_t baseMip_ = 0, mipCount_ = 1;
    uint32_t baseLayer_ = 0, layerCount_ = 1;
    uint16_t srcStage_ = 0, dstStage_ = 0;
};

} // namespace bighero
