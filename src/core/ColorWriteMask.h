#pragma once
#include <cstdint>

namespace bighero {

// ColorWriteMask: a render-target write-mask descriptor controlling which RGBA
// channels are written, with helper combine/test functions.
struct ColorWriteMask {
    enum Mask : uint32_t {
        R = 1u << 0,
        G = 1u << 1,
        B = 1u << 2,
        A = 1u << 3,
        RGB = R | G | B,
        RGBA = R | G | B | A
    };

    uint32_t mask = RGBA;

    ColorWriteMask() = default;
    explicit ColorWriteMask(uint32_t m) : mask(m) {}

    bool WritesRed() const { return (mask & R) != 0; }
    bool WritesGreen() const { return (mask & G) != 0; }
    bool WritesBlue() const { return (mask & B) != 0; }
    bool WritesAlpha() const { return (mask & A) != 0; }
    void EnableRed(bool e) { e ? (mask |= R) : (mask &= ~R); }
    void EnableAll() { mask = RGBA; }
    void DisableAll() { mask = 0; }
    // Combine two masks (channels written by either).
    static uint32_t Combine(uint32_t a, uint32_t b) { return a | b; }
    // Whether all channels in an incoming mask are enabled.
    bool Includes(uint32_t channels) const { return (mask & channels) == channels; }
};

} // namespace bighero
