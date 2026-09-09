#pragma once
#include <cstdint>

namespace bighero {

// ColorComponentMask: bitmask helpers for color write masks (R/G/B/A channels)
// used by blend state. Self-contained, std-lib only.
class ColorComponentMask {
public:
    enum : uint8_t {
        R = 0x1, G = 0x2, B = 0x4, A = 0x8,
        RG = R | G, RGB = R | G | B, RGBA = R | G | B | A
    };

    ColorComponentMask() = default;
    explicit ColorComponentMask(uint8_t mask) : mask_(mask) {}

    void SetMask(uint8_t m) { mask_ = m; }
    uint8_t Mask() const { return mask_; }
    void SetChannel(uint8_t channel) { mask_ |= channel; }
    void ClearChannel(uint8_t channel) { mask_ &= (uint8_t)~channel; }
    bool Has(uint8_t channel) const { return (mask_ & channel) != 0; }
    bool IsFull() const { return mask_ == RGBA; }
    bool IsNone() const { return mask_ == 0; }

    bool WritesRed() const { return Has(R); }
    bool WritesGreen() const { return Has(G); }
    bool WritesBlue() const { return Has(B); }
    bool WritesAlpha() const { return Has(A); }

    static const char* Describe(uint8_t m) {
        switch (m) {
            case R: return "R";
            case RG: return "RG";
            case RGB: return "RGB";
            case RGBA: return "RGBA";
            case 0: return "None";
            default: return "Partial";
        }
    }

private:
    uint8_t mask_ = RGBA;
};

} // namespace bighero
