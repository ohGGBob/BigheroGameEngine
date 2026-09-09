#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// SurfaceFormat: describes the color format and color space of a swapchain
// surface. Self-contained, std-lib only.
class SurfaceFormat {
public:
    enum class Format : uint16_t {
        Undefined = 0,
        RGBA8Unorm = 1, BGRA8Unorm = 2, RGBA8Srgb = 3,
        RGBA16Float = 4, RGBA32Float = 5,
        R8Unorm = 6, RG8Unorm = 7, B8G8R8A8Unorm = 8,
        R16Uint = 9, R32Uint = 10, RGBA8Snorm = 11,
        Depth32Float = 12, Depth24Stencil8 = 13
    };
    enum class ColorSpace : uint8_t {
        SrgbNonLinear = 0, DisplayP3Linear = 1, ExtendedLinear = 2,
        HDR10ST2084 = 3, HDR10HLG = 4
    };

    SurfaceFormat() = default;
    SurfaceFormat(Format fmt, ColorSpace cs = ColorSpace::SrgbNonLinear)
        : format_(fmt), colorSpace_(cs) {}

    void SetFormat(Format f) { format_ = f; }
    Format GetFormat() const { return format_; }
    void SetColorSpace(ColorSpace cs) { colorSpace_ = cs; }
    ColorSpace GetColorSpace() const { return colorSpace_; }

    bool IsValid() const { return format_ != Format::Undefined; }
    bool IsDepthOnly() const {
        return format_ == Format::Depth32Float;
    }
    bool IsDepthStencil() const {
        return format_ == Format::Depth24Stencil8;
    }
    bool IsHDR() const {
        return colorSpace_ == ColorSpace::HDR10ST2084 || colorSpace_ == ColorSpace::HDR10HLG;
    }
    int Channels() const {
        switch (format_) {
            case Format::R8Unorm: case Format::R16Uint: case Format::R32Uint: return 1;
            case Format::RG8Unorm: return 2;
            default: return 4;
        }
    }

    static const char* FormatName(Format f) {
        switch (f) {
            case Format::Undefined: return "Undefined";
            case Format::RGBA8Unorm: return "RGBA8Unorm";
            case Format::BGRA8Unorm: return "BGRA8Unorm";
            case Format::RGBA8Srgb: return "RGBA8Srgb";
            case Format::RGBA16Float: return "RGBA16Float";
            case Format::RGBA32Float: return "RGBA32Float";
            case Format::Depth32Float: return "Depth32Float";
            case Format::Depth24Stencil8: return "Depth24Stencil8";
            default: return "Other";
        }
    }

private:
    Format format_ = Format::Undefined;
    ColorSpace colorSpace_ = ColorSpace::SrgbNonLinear;
};

} // namespace bighero
