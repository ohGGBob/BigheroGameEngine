#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace bighero {

// Buffer: a growable byte buffer with append and compact read helpers.
// Self-contained, std-lib only.
class Buffer {
public:
    Buffer() = default;
    explicit Buffer(size_t initial) { data_.reserve(initial); }

    void Clear() { data_.clear(); writePos_ = 0; readPos_ = 0; }
    size_t Size() const { return data_.size(); }
    bool Empty() const { return data_.empty(); }

    void Append(const void* src, size_t bytes) {
        if (bytes == 0) return;
        const char* p = static_cast<const char*>(src);
        data_.insert(data_.end(), p, p + bytes);
        writePos_ += bytes;
    }
    void AppendByte(uint8_t v) { data_.push_back((char)v); ++writePos_; }
    void AppendU32(uint32_t v) { Append(&v, sizeof(v)); }
    void AppendF32(float v) { Append(&v, sizeof(v)); }
    void AppendI32(int32_t v) { Append(&v, sizeof(v)); }

    // Reserve space and return writable pointer for manual fill.
    char* Alloc(size_t bytes) {
        size_t old = data_.size();
        data_.resize(old + bytes);
        writePos_ += bytes;
        return data_.data() + old;
    }

    const char* Data() const { return data_.data(); }
    char* Data() { return data_.data(); }

    // Read helpers advance an internal read cursor.
    uint8_t ReadByte() { return (uint8_t)data_[readPos_++]; }
    uint32_t ReadU32() { uint32_t v; std::memcpy(&v, data_.data()+readPos_, 4); readPos_ += 4; return v; }
    float ReadF32() { float v; std::memcpy(&v, data_.data()+readPos_, 4); readPos_ += 4; return v; }
    int32_t ReadI32() { int32_t v; std::memcpy(&v, data_.data()+readPos_, 4); readPos_ += 4; return v; }
    void Skip(size_t n) { readPos_ += n; }
    size_t ReadPos() const { return readPos_; }
    void SetReadPos(size_t p) { readPos_ = p; }

    void Reserve(size_t n) { data_.reserve(n); }
    void Shrink() { data_.shrink_to_fit(); }

private:
    std::vector<char> data_;
    size_t writePos_ = 0, readPos_ = 0;
};

} // namespace bighero
