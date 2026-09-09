#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// VertexFormat: describes the arrangement of vertex attributes in a vertex
// buffer (stride + attribute list), used by the renderer to validate draw
// calls against a shader's expected inputs. Self-contained, std-lib only.
class VertexFormat {
public:
    struct Attribute {
        enum class Type { Float, Float2, Float3, Float4, UByte4, Int, Int2, Int4 };

        Type type = Type::Float3;
        // Semantic slot / location (position=0, normal=1, uv=2, etc.).
        int location = 0;
        // Byte offset within the vertex.
        int offset = 0;
        bool normalized = false;
    };

    VertexFormat() = default;
    explicit VertexFormat(int stride) : stride_(stride) {}

    void SetStride(int stride) { stride_ = stride; }
    int Stride() const { return stride_; }

    void AddAttribute(const Attribute& attr) { attrs_.push_back(attr); }
    void Clear() { attrs_.clear(); }
    size_t Count() const { return attrs_.size(); }
    const Attribute* Get(int index) const {
        if (index < 0 || (size_t)index >= attrs_.size()) return nullptr;
        return &attrs_[(size_t)index];
    }

    // Byte size of a single attribute value.
    static int SizeOf(Attribute::Type t) {
        switch (t) {
            case Attribute::Type::Float:   return 4;
            case Attribute::Type::Float2:  return 8;
            case Attribute::Type::Float3:  return 12;
            case Attribute::Type::Float4:  return 16;
            case Attribute::Type::UByte4:  return 4;
            case Attribute::Type::Int:     return 4;
            case Attribute::Type::Int2:    return 8;
            case Attribute::Type::Int4:    return 16;
            default:                       return 4;
        }
    }
    // Total size of all attributes (used to validate stride).
    int ComputeSize() const {
        int sz = 0;
        for (const auto& a : attrs_) sz += SizeOf(a.type);
        return sz;
    }

private:
    int stride_ = 0;
    std::vector<Attribute> attrs_;
};

} // namespace bighero
