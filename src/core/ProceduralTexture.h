#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bighero {

// ProceduralTexture: a CPU-generated texture descriptor plus the generated
// pixel buffer. Provides helper generators (solid, checker, noise fallback).
// Pure standard-library data container.
class ProceduralTexture {
public:
    enum class Generator { Solid, Checker, VerticalGradient, HorizontalGradient };

    ProceduralTexture() {}
    ProceduralTexture(std::size_t w, std::size_t h) { Resize(w, h); }

    void Resize(std::size_t w, std::size_t h) {
        width_ = w; height_ = h; pixels_.assign(w*h, 0xFF);
    }
    std::size_t Width() const { return width_; }
    std::size_t Height() const { return height_; }
    void SetPixel(std::size_t x, std::size_t y, std::uint32_t rgba) {
        if (x < width_ && y < height_) pixels_[y*width_ + x] = rgba;
    }
    std::uint32_t GetPixel(std::size_t x, std::size_t y) const {
        if (x < width_ && y < height_) return pixels_[y*width_ + x];
        return 0;
    }
    const std::vector<std::uint32_t>& Pixels() const { return pixels_; }

    void Generate(Generator g, std::uint32_t a, std::uint32_t b,
                  std::uint32_t bg = 0x0) {
        for (std::size_t y = 0; y < height_; ++y) {
            for (std::size_t x = 0; x < width_; ++x) {
                std::uint32_t c;
                switch (g) {
                    case Generator::Solid: c = a; break;
                    case Generator::Checker:
                        c = (((x/8) ^ (y/8)) & 1) ? a : b; break;
                    case Generator::VerticalGradient:
                        c = (height_ <= 1) ? a : Mix(a, b, (float)y / (float)(height_-1)); break;
                    case Generator::HorizontalGradient:
                        c = (width_ <= 1) ? a : Mix(a, b, (float)x / (float)(width_-1)); break;
                    default: c = bg; break;
                }
                SetPixel(x, y, c);
            }
        }
    }

    void Fill(std::uint32_t c) { Generate(Generator::Solid, c, c, c); }
    std::size_t ByteCount() const { return pixels_.size() * 4; }

private:
    static std::uint32_t Mix(std::uint32_t a, std::uint32_t b, float t) {
        auto ch = [](float v){ std::uint32_t u = (std::uint32_t)(v + 0.5f); return u > 255 ? 255 : u; };
        std::uint32_t ar=(a>>24)&0xFF, ag=(a>>16)&0xFF, ab=(a>>8)&0xFF, aa=a&0xFF;
        std::uint32_t br=(b>>24)&0xFF, bg2=(b>>16)&0xFF, bb=(b>>8)&0xFF, ba=b&0xFF;
        return (ch(ar+(br-ar)*t)<<24) | (ch(ag+(bg2-ag)*t)<<16) |
               (ch(ab+(bb-ab)*t)<<8)  | (ch(aa+(ba-aa)*t));
    }
    std::size_t width_ = 0, height_ = 0;
    std::vector<std::uint32_t> pixels_;
};

} // namespace bighero
