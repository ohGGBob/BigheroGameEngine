#pragma once
#include <cstdint>
#include <cstring>

namespace bighero {

// Minimal 16-bit floating-point (half) encoder/decoder helpers.
// Uses IEEE-754 binary16 layout: 1 sign, 5 exponent, 10 mantissa.
class HalfFloat {
public:
    static std::uint16_t FloatToHalf(float value) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        std::uint32_t sign = (bits >> 16) & 0x8000u;
        std::int32_t exponent = (int32_t)((bits >> 23) & 0xFF) - 127 + 15;
        std::uint32_t mantissa = bits & 0x7FFFFFu;

        if (exponent <= 0) {
            if (exponent < -10) return (std::uint16_t)sign;
            mantissa |= 0x800000u;
            std::uint32_t shift = (std::uint32_t)(14 - exponent);
            std::uint16_t half = (std::uint16_t)(mantissa >> shift);
            return (std::uint16_t)(sign | half);
        }
        if (exponent >= 31) {
            if (((bits >> 23) & 0xFF) == 255 && mantissa) {
                // NaN
                return (std::uint16_t)(sign | 0x7C00u | (mantissa >> 13) | 1u);
            }
            return (std::uint16_t)(sign | 0x7C00u); // inf
        }
        return (std::uint16_t)(sign | (exponent << 10) | (mantissa >> 13));
    }

    static float HalfToFloat(std::uint16_t half) {
        std::uint32_t sign = ((std::uint32_t)half & 0x8000u) << 16;
        std::uint32_t exponent = ((std::uint32_t)half >> 10) & 0x1F;
        std::uint32_t mantissa = (std::uint32_t)half & 0x3FFu;
        std::uint32_t bits;
        if (exponent == 0) {
            if (mantissa == 0) {
                bits = sign; // +-0
            } else {
                // subnormal
                exponent = 1;
                while ((mantissa & 0x400u) == 0) { mantissa <<= 1; --exponent; }
                mantissa &= 0x3FFu;
                std::uint32_t e = exponent + (127 - 15);
                bits = sign | (e << 23) | (mantissa << 13);
            }
        } else if (exponent == 31) {
            bits = sign | 0x7F800000u | (mantissa << 13);
        } else {
            std::uint32_t e = exponent + (127 - 15);
            bits = sign | (e << 23) | (mantissa << 13);
        }
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }
};

} // namespace bighero
