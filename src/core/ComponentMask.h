#pragma once
#include <cstdint>
#include <bit>

namespace bighero {

// Bitmask for identifying which components an entity has (ECS-style).
class ComponentMask {
public:
    static constexpr int MaxBits = 64;

    ComponentMask() : bits_(0) {}
    explicit ComponentMask(uint64_t bits) : bits_(bits) {}

    void Set(int index)   { if (index >= 0 && index < MaxBits) bits_ |= (uint64_t(1) << index); }
    void Clear(int index) { if (index >= 0 && index < MaxBits) bits_ &= ~(uint64_t(1) << index); }
    bool Has(int index) const { return index >= 0 && index < MaxBits && (bits_ & (uint64_t(1) << index)) != 0; }
    void Reset() { bits_ = 0; }

    bool Any(const ComponentMask& other) const { return (bits_ & other.bits_) != 0; }
    bool All(const ComponentMask& other) const { return (bits_ & other.bits_) == other.bits_; }
    bool None(const ComponentMask& other) const { return (bits_ & other.bits_) == 0; }

    void Merge(const ComponentMask& other) { bits_ |= other.bits_; }
    void Sub(const ComponentMask& other) { bits_ &= ~other.bits_; }
    void Intersect(const ComponentMask& other) { bits_ &= other.bits_; }

    uint64_t Bits() const { return bits_; }
    int Count() const { return std::popcount<uint64_t>(bits_); }
    bool Empty() const { return bits_ == 0; }
    bool operator==(const ComponentMask& o) const { return bits_ == o.bits_; }
    bool operator!=(const ComponentMask& o) const { return bits_ != o.bits_; }

private:
    uint64_t bits_;
};

} // namespace bighero
