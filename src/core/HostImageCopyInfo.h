#pragma once
#include <cstdint>

namespace bighero {

// HostImageCopyInfo: describes a host-to-device or device-to-host image copy
// (host pointer, image extent, row/array pitch). Self-contained.
class HostImageCopyInfo {
public:
    HostImageCopyInfo() = default;
    HostImageCopyInfo(uint64_t image, uint32_t width, uint32_t height, uint32_t depth = 1)
        : image_(image), width_(width), height_(height), depth_(depth) {}

    void SetImage(uint64_t img) { image_ = img; }
    uint64_t Image() const { return image_; }
    void SetExtent(uint32_t w, uint32_t h, uint32_t d = 1) {
        width_=w; height_=h; depth_=d;
    }
    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    uint32_t Depth() const { return depth_; }

    void SetHostPointer(uint64_t ptr) { hostPtr_ = ptr; }
    uint64_t HostPointer() const { return hostPtr_; }
    void SetRowPitch(uint64_t p) { rowPitch_ = p; }
    uint64_t RowPitch() const { return rowPitch_; }
    void SetSlicePitch(uint64_t p) { slicePitch_ = p; }
    uint64_t SlicePitch() const { return slicePitch_; }

    void SetMipLevel(uint32_t m) { mipLevel_ = m; }
    uint32_t MipLevel() const { return mipLevel_; }
    void SetBaseArrayLayer(uint32_t l) { baseLayer_ = l; }
    uint32_t BaseArrayLayer() const { return baseLayer_; }
    void SetLayerCount(uint32_t c) { layerCount_ = c; }
    uint32_t LayerCount() const { return layerCount_; }

    uint64_t PixelCount() const { return (uint64_t)width_ * height_ * depth_; }
    bool IsValid() const { return image_ != 0 && width_ > 0 && height_ > 0; }
    static const char* DirectionName(bool toDevice) { return toDevice ? "HostToDevice" : "DeviceToHost"; }

private:
    uint64_t image_ = 0;
    uint32_t width_ = 0, height_ = 0, depth_ = 1;
    uint64_t hostPtr_ = 0, rowPitch_ = 0, slicePitch_ = 0;
    uint32_t mipLevel_ = 0, baseLayer_ = 0, layerCount_ = 1;
};

} // namespace bighero
