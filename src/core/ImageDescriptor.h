#pragma once
#include <cstdint>

namespace bighero {

// ImageDescriptor: describes a GPU image's dimensions, format usage, and
// mip level allocation strategy. Self-contained, std-lib only.
class ImageDescriptor {
public:
    enum class Usage : uint16_t {
        ColorAttachment   = 1,
        DepthStencilAttachment = 2,
        Sampled           = 4,
        Storage           = 8,
        TransferSrc       = 16,
        TransferDst       = 32
    };

    ImageDescriptor() = default;
    ImageDescriptor(uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels)
        : width_(width), height_(height), depth_(depth), mipLevels_(mipLevels) {}

    void SetExtent(uint32_t w, uint32_t h, uint32_t d = 1) {
        width_=w; height_=h; depth_=d;
    }
    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    uint32_t Depth() const { return depth_; }

    void SetMipLevels(uint32_t m) { mipLevels_ = m; }
    uint32_t MipLevels() const { return mipLevels_; }
    void SetArrayLayers(uint32_t l) { layers_ = l; }
    uint32_t ArrayLayers() const { return layers_; }
    void SetSamples(uint32_t s) { samples_ = s; }
    uint32_t Samples() const { return samples_; }

    void AddUsage(Usage u) { usage_ |= static_cast<uint16_t>(u); }
    void SetUsage(uint16_t u) { usage_ = u; }
    uint16_t UsageFlags() const { return usage_; }
    bool HasUsage(Usage u) const { return (usage_ & static_cast<uint16_t>(u)) != 0; }

    uint64_t PixelCount() const {
        return (uint64_t)width_ * height_ * depth_ * layers_;
    }
    bool IsValid() const { return width_ > 0 && height_ > 0 && depth_ > 0; }

private:
    uint32_t width_ = 1, height_ = 1, depth_ = 1;
    uint32_t mipLevels_ = 1, layers_ = 1, samples_ = 1;
    uint16_t usage_ = 0;
};

} // namespace bighero
