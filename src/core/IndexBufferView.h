#pragma once
#include <cstdint>

namespace bighero {

// IndexBufferView: describes a view into an index buffer (offset, size, and
// index element type). Self-contained, std-lib only.
class IndexBufferView {
public:
    enum class IndexType : uint8_t { Uint16 = 0, Uint32 = 1, Uint8 = 2 };

    IndexBufferView() = default;
    IndexBufferView(uint64_t offset, uint64_t size, IndexType type)
        : offset_(offset), size_(size), type_(type) {}

    void SetOffset(uint64_t o) { offset_ = o; }
    uint64_t Offset() const { return offset_; }
    void SetSize(uint64_t s) { size_ = s; }
    uint64_t Size() const { return size_; }
    void SetIndexType(IndexType t) { type_ = t; }
    IndexType GetIndexType() const { return type_; }

    uint32_t BytesPerIndex() const {
        switch (type_) {
            case IndexType::Uint8: return 1;
            case IndexType::Uint16: return 2;
            case IndexType::Uint32: return 4;
        }
        return 4;
    }
    uint64_t IndexCount() const { return BytesPerIndex() ? size_ / BytesPerIndex() : 0; }
    bool IsValid() const { return size_ > 0 && size_ % BytesPerIndex() == 0; }
    uint64_t EndOffset() const { return offset_ + size_; }

    static const char* TypeName(IndexType t) {
        switch (t) {
            case IndexType::Uint8: return "Uint8";
            case IndexType::Uint16: return "Uint16";
            case IndexType::Uint32: return "Uint32";
        }
        return "Unknown";
    }

private:
    uint64_t offset_ = 0;
    uint64_t size_ = 0;
    IndexType type_ = IndexType::Uint16;
};

} // namespace bighero
