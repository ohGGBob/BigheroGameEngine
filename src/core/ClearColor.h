#pragma once
#include <cstdint>

namespace bighero {

// ClearColor: a color used for render-target clear operations (RGBA float).
// Self-contained, std-lib only.
class ClearColor {
public:
    ClearColor() = default;
    ClearColor(float r, float g, float b, float a = 1.0f)
        : r_(r), g_(g), b_(b), a_(a) {}

    static ClearColor Black() { return ClearColor(0, 0, 0, 1); }
    static ClearColor White() { return ClearColor(1, 1, 1, 1); }
    static ClearColor Transparent() { return ClearColor(0, 0, 0, 0); }
    static ClearColor Clear() { return ClearColor(0, 0, 0, 0); }

    void Set(float r, float g, float b, float a = 1.0f) {
        r_ = r; g_ = g; b_ = b; a_ = a;
    }
    float R() const { return r_; }
    float G() const { return g_; }
    float B() const { return b_; }
    float A() const { return a_; }
    void SetR(float r) { r_ = r; }
    void SetG(float g) { g_ = g; }
    void SetB(float b) { b_ = b; }
    void SetA(float a) { a_ = a; }

    bool IsOpaque() const { return a_ >= 1.0f; }
    bool IsTransparent() const { return a_ <= 0.0f; }

    uint32_t ToRGBA8() const {
        uint32_t r = (uint32_t)(r_ * 255.0f + 0.5f);
        uint32_t g = (uint32_t)(g_ * 255.0f + 0.5f);
        uint32_t b = (uint32_t)(b_ * 255.0f + 0.5f);
        uint32_t a = (uint32_t)(a_ * 255.0f + 0.5f);
        return (r << 24) | (g << 16) | (b << 8) | a;
    }

    void Pack(float out[4]) const { out[0]=r_; out[1]=g_; out[2]=b_; out[3]=a_; }

private:
    float r_ = 0, g_ = 0, b_ = 0, a_ = 1.0f;
};

} // namespace bighero
