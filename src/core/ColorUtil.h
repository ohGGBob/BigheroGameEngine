#pragma once
#include <cmath>
#include <cstdint>

namespace bighero {

// ColorUtil: color-space conversion and palette helpers (sRGB <-> linear,
// byte <-> float, HSL <-> RGB, random pastel). Self-contained.
class ColorUtil {
public:
    // Convert an sRGB gamma-encoded value to linear space.
    static float SRGBToLinear(float c) {
        if (c <= 0.04045f) return c / 12.92f;
        return std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    // Convert a linear value back to sRGB gamma-encoded space.
    static float LinearToSRGB(float c) {
        if (c <= 0.0031308f) return c * 12.92f;
        return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }
    static uint8_t FloatToByte(float v) {
        if (v <= 0) return 0;
        if (v >= 1) return 255;
        return (uint8_t)(v * 255.0f + 0.5f);
    }
    static float ByteToFloat(uint8_t v) { return v / 255.0f; }
    // Convert hue-saturation-lightness (each in [0,1]) to RGB float[3].
    static void HSLToRGB(float h, float s, float l, float& r, float& g, float& b) {
        if (s <= 0) { r = g = b = l; return; }
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
        float p = 2 * l - q;
        r = HueToRGB(p, q, h + 1.0f / 3.0f);
        g = HueToRGB(p, q, h);
        b = HueToRGB(p, q, h - 1.0f / 3.0f);
    }
    static void RGBToHSL(float r, float g, float b, float& h, float& s, float& l) {
        float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        l = (mx + mn) * 0.5f;
        if (mx == mn) { h = 0; s = 0; return; }
        float d = mx - mn;
        s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
        if (mx == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h /= 6.0f;
    }
    // Blend two RGB float triples.
    static void Blend(float r1, float g1, float b1,
                      float r2, float g2, float b2, float t,
                      float& r, float& g, float& b) {
        r = r1 + (r2 - r1) * t;
        g = g1 + (g2 - g1) * t;
        b = b1 + (b2 - b1) * t;
    }

private:
    static float HueToRGB(float p, float q, float t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
        if (t < 1.0f / 2.0f) return q;
        if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    }
};

} // namespace bighero
