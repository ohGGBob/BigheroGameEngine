#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace bighero {

// CPU-side vertex buffer: a typed array of raw vertices with a declared
// vertex size and capacity. Tracks how many vertices have actually been
// written so PushVertex can add beyond the cursor.
class VertexBuffer {
public:
    VertexBuffer() {}
    VertexBuffer(std::size_t vertexSize, std::size_t capacity)
        : vertexSize_(vertexSize), capacity_(capacity),
          data_(vertexSize * capacity) {}

    void Allocate(std::size_t vertexSize, std::size_t capacity) {
        vertexSize_ = vertexSize;
        capacity_ = capacity;
        written_ = 0;
        data_.assign(vertexSize * capacity, 0);
    }

    // Write one vertex at index (must be within capacity).
    void SetVertex(std::size_t index, const void* src) {
        std::size_t off = index * vertexSize_;
        if (off + vertexSize_ > data_.size()) return;
        std::memcpy(&data_[off], src, vertexSize_);
        if (index + 1 > written_) written_ = index + 1;
    }

    // Append a vertex at the cursor if there is room; returns false if full.
    bool PushVertex(const void* src) {
        if (vertexSize_ == 0 || written_ >= capacity_) return false;
        std::memcpy(&data_[written_ * vertexSize_], src, vertexSize_);
        ++written_;
        return true;
    }

    std::size_t Capacity() const { return capacity_; }
    std::size_t Count() const { return written_; }
    std::size_t VertexSize() const { return vertexSize_; }
    std::size_t ByteSize() const { return data_.size(); }
    const std::uint8_t* Data() const { return data_.data(); }
    bool Empty() const { return written_ == 0; }
    void Clear() { written_ = 0; std::memset(data_.data(), 0, data_.size()); }

private:
    std::size_t vertexSize_ = 0;
    std::size_t capacity_ = 0;
    std::size_t written_ = 0;
    std::vector<std::uint8_t> data_;
};

} // namespace bighero
