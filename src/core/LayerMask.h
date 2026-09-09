#pragma once
#include <cstdint>

namespace bighero {

// LayerMask: a bitmask of up to 32 layers for collision/camera/everything
// filtering. Provides set/test/toggle and named layer helpers. Pure stdlib.
class LayerMask {
public:
    LayerMask() {}
    explicit LayerMask(std::uint32_t mask) : mask_(mask) {}

    void Set(int layer) { if (layer >= 0 && layer < 32) mask_ |= (1u << layer); }
    void Clear(int layer) { if (layer >= 0 && layer < 32) mask_ &= ~(1u << layer); }
    void Toggle(int layer) { if (layer >= 0 && layer < 32) mask_ ^= (1u << layer); }
    bool Contains(int layer) const {
        return layer >= 0 && layer < 32 && (mask_ & (1u << layer));
    }
    void SetAll() { mask_ = 0xFFFFFFFFu; }
    void SetNone() { mask_ = 0; }
    std::uint32_t Mask() const { return mask_; }
    void SetMask(std::uint32_t m) { mask_ = m; }

    int Count() const {
        int n = 0; std::uint32_t m = mask_;
        while (m) { n += (m & 1); m >>= 1; }
        return n;
    }
    bool IsEmpty() const { return mask_ == 0; }
    bool IsFull() const { return mask_ == 0xFFFFFFFFu; }

    // Bitwise combination helpers.
    LayerMask operator&(const LayerMask& o) const { return LayerMask(mask_ & o.mask_); }
    LayerMask operator|(const LayerMask& o) const { return LayerMask(mask_ | o.mask_); }
    LayerMask operator~() const { return LayerMask(~mask_); }
    bool operator==(const LayerMask& o) const { return mask_ == o.mask_; }

    // Mask with only `layer` set.
    static LayerMask Single(int layer) {
        LayerMask m; m.Set(layer); return m;
    }

private:
    std::uint32_t mask_ = 0;
};

} // namespace bighero
