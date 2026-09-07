#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// CPU-side index buffer: 16- or 32-bit indices for indexed draws.
class IndexBuffer {
public:
    enum class IndexType { U16, U32 };

    explicit IndexBuffer(IndexType type = IndexType::U32)
        : type_(type) {}

    void SetType(IndexType t) { type_ = t; }

    void AddIndex(std::uint32_t idx) {
        if (type_ == IndexType::U16) {
            if (idx <= 0xFFFFu) u16_.push_back((std::uint16_t)idx);
        } else {
            u32_.push_back(idx);
        }
    }

    std::size_t Count() const {
        return type_ == IndexType::U16 ? u16_.size() : u32_.size();
    }
    std::size_t ByteSize() const {
        return type_ == IndexType::U16 ? u16_.size() * 2 : u32_.size() * 4;
    }
    std::uint32_t At(std::size_t i) const {
        return type_ == IndexType::U16 ? u16_[i] : u32_[i];
    }
    bool Empty() const { return Count() == 0; }
    void Clear() { u16_.clear(); u32_.clear(); }

private:
    IndexType type_;
    std::vector<std::uint16_t> u16_;
    std::vector<std::uint32_t> u32_;
};

} // namespace bighero
