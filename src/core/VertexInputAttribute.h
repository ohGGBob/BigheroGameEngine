#pragma once
#include <cstdint>

namespace bighero {

// VertexInputAttribute: describes a single vertex attribute (location,
// binding, format, byte offset). Self-contained, std-lib only.
class VertexInputAttribute {
public:
    enum class Format : uint8_t {
        Float = 0, Float2 = 1, Float3 = 2, Float4 = 3,
        Int = 4, Int2 = 5, Int3 = 6, Int4 = 7,
        Uint = 8, Uint2 = 9, Uint3 = 10, Uint4 = 11,
        Ubyte4Norm = 12, Short2 = 13, Short4 = 14
    };

    VertexInputAttribute() = default;
    VertexInputAttribute(uint32_t location, uint32_t binding, Format format, uint32_t offset = 0)
        : location_(location), binding_(binding), format_(format), offset_(offset) {}

    void SetLocation(uint32_t l) { location_ = l; }
    uint32_t Location() const { return location_; }
    void SetBinding(uint32_t b) { binding_ = b; }
    uint32_t Binding() const { return binding_; }
    void SetFormat(Format f) { format_ = f; }
    Format GetFormat() const { return format_; }
    void SetOffset(uint32_t o) { offset_ = o; }
    uint32_t Offset() const { return offset_; }

    uint32_t ComponentCount() const {
        switch (format_) {
            case Format::Float2: case Format::Int2: case Format::Uint2:
            case Format::Short2: return 2;
            case Format::Float3: case Format::Int3: case Format::Uint3: return 3;
            case Format::Float4: case Format::Int4: case Format::Uint4:
            case Format::Ubyte4Norm: case Format::Short4: return 4;
            default: return 1;
        }
    }
    uint32_t BytesPerElement() const {
        switch (format_) {
            case Format::Float4: case Format::Int4: case Format::Uint4: return 16;
            case Format::Float3: case Format::Int3: case Format::Uint3: return 12;
            case Format::Float2: case Format::Int2: case Format::Uint2:
            case Format::Short4: return 8;
            case Format::Float: case Format::Int: case Format::Uint: return 4;
            case Format::Ubyte4Norm: return 4;
            case Format::Short2: return 4;
        }
        return 4;
    }

private:
    uint32_t location_ = 0;
    uint32_t binding_ = 0;
    Format format_ = Format::Float;
    uint32_t offset_ = 0;
};

} // namespace bighero
