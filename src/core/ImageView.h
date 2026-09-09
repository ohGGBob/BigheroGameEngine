#pragma once
#include <cstdint>
#include "SurfaceFormat_v2.h"

namespace bighero {

// ImageView: describes a view into an image (subresource range, format,
// swizzle). Self-contained, std-lib only.
class ImageView {
public:
    enum class ViewType : uint8_t { Tex2D = 0, Tex2DArray = 1, TexCube = 2, Tex3D = 3 };

    ImageView() = default;
    ImageView(uint64_t image, ViewType type, SurfaceFormat format)
        : image_(image), type_(type), format_(format) {}

    void SetImage(uint64_t img) { image_ = img; }
    uint64_t Image() const { return image_; }
    void SetType(ViewType t) { type_ = t; }
    ViewType GetType() const { return type_; }
    void SetFormat(SurfaceFormat f) { format_ = f; }
    SurfaceFormat Format() const { return format_; }

    void SetBaseMipLevel(uint32_t m) { baseMip_ = m; }
    uint32_t BaseMipLevel() const { return baseMip_; }
    void SetMipLevelCount(uint32_t c) { mipCount_ = c; }
    uint32_t MipLevelCount() const { return mipCount_; }
    void SetBaseArrayLayer(uint32_t l) { baseLayer_ = l; }
    uint32_t BaseArrayLayer() const { return baseLayer_; }
    void SetLayerCount(uint32_t c) { layerCount_ = c; }
    uint32_t LayerCount() const { return layerCount_; }

    void SetSwizzle(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        swizzle_[0]=r; swizzle_[1]=g; swizzle_[2]=b; swizzle_[3]=a;
    }
    const uint8_t* Swizzle() const { return swizzle_; }

    bool IsValid() const { return image_ != 0 && format_.IsValid(); }
    static const char* TypeName(ViewType t) {
        switch (t) {
            case ViewType::Tex2D: return "Tex2D";
            case ViewType::Tex2DArray: return "Tex2DArray";
            case ViewType::TexCube: return "TexCube";
            case ViewType::Tex3D: return "Tex3D";
        }
        return "Unknown";
    }

private:
    uint64_t image_ = 0;
    ViewType type_ = ViewType::Tex2D;
    SurfaceFormat format_;
    uint32_t baseMip_ = 0, mipCount_ = 1;
    uint32_t baseLayer_ = 0, layerCount_ = 1;
    uint8_t swizzle_[4] = {0,1,2,3};
};

} // namespace bighero
