#pragma once
#include <vector>
#include <string>
#include <cstddef>

namespace bighero {

// Describes the layout of vertex attributes inside a vertex buffer: an
// ordered list of (name, component count, type) elements with per-element
// offset/stride. Used to bind buffers to shader attributes.
class VertexLayout {
public:
    enum class Type { Float, Vec2, Vec3, Vec4, UByte4, Int };

    struct Element {
        std::string name;
        Type type = Type::Float;
        int location = 0;
        std::size_t offset = 0;     // byte offset within vertex
        bool normalized = false;
    };

    int AddElement(const char* name, Type type, int location = -1,
                   bool normalized = false) {
        std::size_t off = stride_;
        stride_ += TypeSize(type);
        int loc = location >= 0 ? location : (int)elements_.size();
        elements_.push_back({std::string(name), type, loc, off, normalized});
        return loc;
    }

    std::size_t Stride() const { return stride_; }
    std::size_t ElementCount() const { return elements_.size(); }
    bool Empty() const { return elements_.empty(); }
    const Element& At(std::size_t i) const { return elements_[i]; }

    static std::size_t TypeSize(Type t) {
        switch (t) {
            case Type::Float:  return 4;
            case Type::Vec2:   return 8;
            case Type::Vec3:   return 12;
            case Type::Vec4:   return 16;
            case Type::UByte4: return 4;
            case Type::Int:    return 4;
        }
        return 4;
    }

private:
    std::vector<Element> elements_;
    std::size_t stride_ = 0;
};

} // namespace bighero
