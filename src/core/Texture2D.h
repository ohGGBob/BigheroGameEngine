#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// Simple RGBA8 2D texture container with set/get pixel and resize.
class Texture2D {
public:
    Texture2D() : width_(0), height_(0) {}
    Texture2D(int w, int h) : width_(w), height_(h), data_((size_t)w * h * 4, 0) {}

    void Resize(int w, int h) {
        width_ = w; height_ = h;
        data_.assign((size_t)w * h * 4, 0);
    }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool Empty() const { return width_ == 0 || height_ == 0; }

    void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
        size_t i = ((size_t)y * width_ + x) * 4;
        data_[i] = r; data_[i+1] = g; data_[i+2] = b; data_[i+3] = a;
    }
    void GetPixel(int x, int y, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) const {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) { r=g=b=a=0; return; }
        size_t i = ((size_t)y * width_ + x) * 4;
        r = data_[i]; g = data_[i+1]; b = data_[i+2]; a = data_[i+3];
    }

    // Fill solid color.
    void Fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
                SetPixel(x, y, r, g, b, a);
    }

    // Set alpha channel (common for textures).
    void SetAlpha(uint8_t a) {
        for (size_t i = 0; i + 3 < data_.size(); i += 4) data_[i+3] = a;
    }

    const uint8_t* Data() const { return data_.data(); }
    uint8_t* Data() { return data_.data(); }
    size_t ByteSize() const { return data_.size(); }

    // Bicubic-ish nearest sampling for readback.
    uint32_t GetPixelRGBA(int x, int y) const {
        uint8_t r,g,b,a; GetPixel(x,y,r,g,b,a);
        return ((uint32_t)r<<24)|((uint32_t)g<<16)|((uint32_t)b<<8)|a;
    }

private:
    int width_, height_;
    std::vector<uint8_t> data_;
};

} // namespace bighero
