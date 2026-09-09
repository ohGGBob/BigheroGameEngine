#pragma once
#include <cstdint>

namespace bighero {

// Flags: a small bitmask wrapper for a fixed-size integer flag set.
// Self-contained, std-lib only.
template <class T = uint32_t>
class Flags {
public:
    using Mask = T;

    Flags() = default;
    explicit Flags(Mask m) : mask_(m) {}

    void Set(Mask f) { mask_ |= f; }
    void Clear(Mask f) { mask_ &= ~f; }
    void Toggle(Mask f) { mask_ ^= f; }
    void SetAll(Mask m) { mask_ = m; }
    void ClearAll() { mask_ = 0; }
    bool IsSet(Mask f) const { return (mask_ & f) != 0; }
    bool None() const { return mask_ == 0; }
    bool Any() const { return mask_ != 0; }
    Mask Value() const { return mask_; }

    Flags& operator|=(Mask f) { Set(f); return *this; }
    Flags& operator&=(Mask f) { mask_ &= f; return *this; }
    Flags& operator^=(Mask f) { Toggle(f); return *this; }
    bool operator==(const Flags& o) const { return mask_ == o.mask_; }
    bool operator!=(const Flags& o) const { return mask_ != o.mask_; }

    // Count of set bits.
    int Count() const {
        Mask m = mask_; int c = 0;
        while (m) { c += (m & 1); m >>= 1; }
        return c;
    }

private:
    Mask mask_ = 0;
};

} // namespace bighero
