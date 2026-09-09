#pragma once
#include <cstdint>
#include "ImageLayout.h"

namespace bighero {

// DescriptorImageInfo: describes an image view + sampler bound to a shader
// descriptor (image layout, sampler). Self-contained, std-lib only.
class DescriptorImageInfo {
public:
    DescriptorImageInfo() = default;
    DescriptorImageInfo(uint64_t sampler, uint64_t imageView, ImageLayout::Layout layout)
        : sampler_(sampler), imageView_(imageView), layout_(layout) {}

    void SetSampler(uint64_t s) { sampler_ = s; }
    uint64_t Sampler() const { return sampler_; }
    void SetImageView(uint64_t v) { imageView_ = v; }
    uint64_t ImageView() const { return imageView_; }
    void SetLayout(ImageLayout::Layout l) { layout_ = l; }
    ImageLayout::Layout GetLayout() const { return layout_; }

    bool HasSampler() const { return sampler_ != 0; }
    bool HasImageView() const { return imageView_ != 0; }
    bool IsValid() const { return imageView_ != 0; }
    bool IsCombined() const { return sampler_ != 0 && imageView_ != 0; }
    static const char* LayoutName(ImageLayout::Layout l) {
        switch (l) {
            case ImageLayout::Layout::ColorAttachment: return "ColorAttachment";
            case ImageLayout::Layout::ShaderReadOnly: return "ShaderReadOnly";
            case ImageLayout::Layout::General: return "General";
            default: return "Other";
        }
    }

private:
    uint64_t sampler_ = 0, imageView_ = 0;
    ImageLayout::Layout layout_ = ImageLayout::Layout::ShaderReadOnly;
};

} // namespace bighero
