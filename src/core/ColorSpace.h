#pragma once
#include <cstdint>
#include <cmath>

namespace bighero {

// ColorSpace: describes the color-space encoding used by a texture or render
// target. Pure enum-based configuration container.
class ColorSpace {
public:
    enum class Space { SRGB, Linear, Gamma };

    ColorSpace() {}
    explicit ColorSpace(Space s) : space_(s) {}

    void SetSpace(Space s) { space_ = s; }
    Space Current() const { return space_; }
    bool IsLinear() const { return space_ == Space::Linear; }
    bool IsSRGB() const { return space_ == Space::SRGB; }

    // Convert sRGB gamma-encoded value to linear (per-channel).
    static float ToLinear(float c) {
        return c <= 0.04045f ? c / 12.92f
                             : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    // Convert linear value to sRGB gamma-encoded.
    static float ToSRGB(float c) {
        return c <= 0.0031308f ? c * 12.92f
                               : 1.055f * std::pow(c, 1.0f/2.4f) - 0.055f;
    }

private:
    Space space_ = Space::SRGB;
};

} // namespace bighero
