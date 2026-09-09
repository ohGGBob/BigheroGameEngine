#pragma once
#include <cstdint>

namespace bighero {

// IndexFormat: describes the digital format of index buffer elements (u16/u32)
// with size/type helpers used by the draw-call path. Self-contained.
struct IndexFormat {
    enum class Type { UInt16, UInt32 };

    Type type = Type::UInt32;

    IndexFormat() = default;
    explicit IndexFormat(Type t) : type(t) {}

    int BytesPerIndex() const { return type == Type::UInt16 ? 2 : 4; }
    // Maximum representable index value for this type.
    uint32_t MaxValue() const { return type == Type::UInt16 ? 0xFFFFu : 0xFFFFFFFFu; }
    // Number of indices that fit in a given byte count.
    size_t CountForBytes(size_t bytes) const {
        int bpi = BytesPerIndex();
        return bpi > 0 ? bytes / bpi : 0;
    }
    // Whether a candidate index value is representable by this format.
    bool Fits(uint64_t value) const { return value <= MaxValue(); }
};

} // namespace bighero
