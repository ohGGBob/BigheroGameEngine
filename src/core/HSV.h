#pragma once
#include <cmath>
#include "Color32.h"

namespace bighero {

// HSV <-> RGB conversion helpers (hue in [0,360), s/v in [0,1]).
namespace HSV {
    inline void ToRGB(float h, float s, float v, float& r, float& g, float& b) {
        if (s <= 0) { r = g = b = v; return; }
        h = std::fmod(h, 360.0f); if (h < 0) h += 360.0f;
        float c = v * s;
        float hp = h / 60.0f;
        float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
        float rr=0, gg=0, bb=0;
        int ip = (int)hp;
        switch (ip % 6) {
            case 0: rr=c; gg=x; break;
            case 1: rr=x; gg=c; break;
            case 2: gg=c; bb=x; break;
            case 3: gg=x; bb=c; break;
            case 4: rr=x; bb=c; break;
            default: rr=c; bb=x; break;
        }
        float m = v - c;
        r = rr + m; g = gg + m; b = bb + m;
    }

    inline Color32 FromHSV(float h, float s, float v, float a = 1.0f) {
        float r, g, b;
        ToRGB(h, s, v, r, g, b);
        return Color32((uint8_t)(r*255), (uint8_t)(g*255), (uint8_t)(b*255), (uint8_t)(a*255));
    }
}

} // namespace bighero
