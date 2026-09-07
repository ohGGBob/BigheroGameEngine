#pragma once
#include <cmath>
#include "Color32.h"

namespace bighero {

// HSL <-> RGB conversion helpers (hue in [0,360), s/l in [0,1]).
namespace HSL {
    inline void ToRGB(float h, float s, float l, float& r, float& g, float& b) {
        auto hue2rgb = [](float p, float q, float t){
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
            if (t < 1.0f/2.0f) return q;
            if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
            return p;
        };
        if (s <= 0) { r = g = b = l; return; }
        h = std::fmod(h, 360.0f); if (h < 0) h += 360.0f;
        h /= 360.0f;
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
        float p = 2 * l - q;
        r = hue2rgb(p, q, h + 1.0f/3.0f);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1.0f/3.0f);
    }

    inline Color32 FromHSL(float h, float s, float l, float a = 1.0f) {
        float r, g, b;
        ToRGB(h, s, l, r, g, b);
        return Color32((uint8_t)(r*255), (uint8_t)(g*255), (uint8_t)(b*255), (uint8_t)(a*255));
    }
}

} // namespace bighero
