#pragma once
#include <cstdint>

namespace bighero {

// IndexType: enumeration of index buffer element widths, with helpers.
// Self-contained, std-lib only.
class IndexType {
public:
    enum class Type : uint8_t { Uint16=0, Uint32=1, None=2 };

    static uint32_t ByteSize(Type t) {
        switch (t) { case Type::Uint16: return 2; case Type::Uint32: return 4; default: return 0; }
    }
    static const char* Name(Type t) {
        switch (t) { case Type::Uint16: return "Uint16"; case Type::Uint32: return "Uint32"; default: return "None"; }
    }
    static bool IsValid(Type t) { return t==Type::Uint16 || t==Type::Uint32; }
    static uint64_t CountFromBytes(uint64_t bytes, Type t) {
        uint32_t sz = ByteSize(t);
        return sz ? bytes/sz : 0;
    }
};

} // namespace bighero
