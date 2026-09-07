#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// 32-bit RGBA color with helper conversions.
struct Color32 {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    Color32() {}
    Color32(uint8_t pr, uint8_t pg, uint8_t pb, uint8_t pa = 255)
        : r(pr), g(pg), b(pb), a(pa) {}

    static Color32 FromHex(uint32_t hex) {
        return Color32((uint8_t)((hex >> 24) & 0xFF),
                       (uint8_t)((hex >> 16) & 0xFF),
                       (uint8_t)((hex >> 8) & 0xFF),
                       (uint8_t)(hex & 0xFF));
    }
    uint32_t ToHex() const {
        return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | a;
    }

    // Linear blending.
    Color32 Lerp(const Color32& other, float t) const {
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        return Color32((uint8_t)(r + (other.r - r) * t),
                       (uint8_t)(g + (other.g - g) * t),
                       (uint8_t)(b + (other.b - b) * t),
                       (uint8_t)(a + (other.a - a) * t));
    }

    // Multiply by scalar (brightness), clamped.
    Color32 Multiply(float f) const {
        auto cl = [f](uint8_t c){ return (uint8_t)(c * f > 255 ? 255 : c * f); };
        return Color32(cl(r), cl(g), cl(b), a);
    }

    static Color32 White() { return Color32(255,255,255,255); }
    static Color32 Black() { return Color32(0,0,0,255); }
    static Color32 Red()   { return Color32(255,0,0,255); }
    static Color32 Green() { return Color32(0,255,0,255); }
    static Color32 Blue()  { return Color32(0,0,255,255); }
    static Color32 Transparent() { return Color32(0,0,0,0); }

    bool operator==(const Color32& o) const { return r==o.r && g==o.g && b==o.b && a==o.a; }
    bool operator!=(const Color32& o) const { return !(*this == o); }
};

} // namespace bighero
