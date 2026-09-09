#pragma once
#include <cstddef>

namespace bighero {

// TextureFormat: describes the texel layout/compression of a texture.
// Pure descriptor with size helpers for computing memory footprint.
class TextureFormat {
public:
    enum class Format {
        Unknown,
        R8, RG8, RGB8, RGBA8,
        R16, RG16, RGB16, RGBA16,
        R32F, RG32F, RGB32F, RGBA32F,
        R8_SNorm, RGBA8_SNorm,
        R16F, RGBA16F,
        D16, D24S8, D32F,
        BC1, BC3, BC5, BC7
    };

    TextureFormat() {}
    explicit TextureFormat(Format f) : format_(f) {}

    void SetFormat(Format f) { format_ = f; }
    Format Current() const { return format_; }

    // Bytes per pixel for non-block formats; 0 for compressed blocks.
    std::size_t BytesPerPixel() const {
        switch (format_) {
            case Format::R8: case Format::R8_SNorm: return 1;
            case Format::RG8: return 2;
            case Format::RGB8: return 3;
            case Format::RGBA8: case Format::RGBA8_SNorm: return 4;
            case Format::R16: return 2;
            case Format::RG16: return 4;
            case Format::RGB16: return 6;
            case Format::RGBA16: return 8;
            case Format::R32F: return 4;
            case Format::RG32F: return 8;
            case Format::RGB32F: return 12;
            case Format::RGBA32F: return 16;
            case Format::R16F: return 2;
            case Format::RGBA16F: return 8;
            case Format::D16: return 2;
            case Format::D24S8: return 4;
            case Format::D32F: return 4;
            default: return 0;
        }
    }

    bool IsCompressed() const {
        return format_ == Format::BC1 || format_ == Format::BC3 ||
               format_ == Format::BC5 || format_ == Format::BC7;
    }
    bool IsDepthStencil() const {
        return format_ == Format::D16 || format_ == Format::D24S8 ||
               format_ == Format::D32F;
    }
    bool IsValid() const { return format_ != Format::Unknown; }

private:
    Format format_ = Format::Unknown;
};

} // namespace bighero
