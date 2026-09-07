#pragma once
#include <cmath>
#include <algorithm>

namespace bighero {

// Tone mapping operators to convert HDR linear values into LDR [0,1] range.
class ToneMapping {
public:
    // Reinhard operator (simple, x/(1+x)).
    static float Reinhard(float x) {
        return x / (1.0f + x);
    }

    // Reinhard with luminance-preserving exposure.
    static float ReinhardExposure(float x, float exposure) {
        return (x * exposure) / (1.0f + x * exposure);
    }

    // ACES filmic approximation (Narkowicz). Input ~[0,1], output ~[0,1].
    static float ACES(float x) {
        const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
        return std::max(0.0f, std::min(1.0f,
            (x * (a * x + b)) / (x * (c * x + d) + e)));
    }

    // Gamma correction: linear [0,1] -> sRGB-like curve.
    static float Gamma(float x, float gamma = 2.2f) {
        if (x <= 0.0f) return 0.0f;
        return std::pow(std::min(1.0f, x), 1.0f / gamma);
    }

    // Simple clamp + gamma chain.
    static float Process(float x, float exposure, float gamma) {
        return Gamma(ReinhardExposure(x, exposure), gamma);
    }

    static void ProcessRGB(float& r, float& g, float& b, float exposure, float gamma) {
        r = Process(r, exposure, gamma);
        g = Process(g, exposure, gamma);
        b = Process(b, exposure, gamma);
    }
};

} // namespace bighero
