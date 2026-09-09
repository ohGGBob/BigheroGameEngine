#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace bighero {

// Serializer: packs scalar values into a byte stream and unpacks them back.
// Handles primitive ints/floats, strings and byte blobs. Self-contained,
// std-lib only.
class Serializer {
public:
    Serializer() = default;

    void Reset() { bytes_.clear(); }
    const std::vector<char>& Bytes() const { return bytes_; }
    size_t Size() const { return bytes_.size(); }

    void WriteByte(uint8_t v) { bytes_.push_back((char)v); }
    void WriteI32(int32_t v) { Append(&v, sizeof(v)); }
    void WriteU32(uint32_t v) { Append(&v, sizeof(v)); }
    void WriteF32(float v) { Append(&v, sizeof(v)); }
    void WriteF64(double v) { Append(&v, sizeof(v)); }
    void WriteBool(bool v) { WriteByte(v ? 1 : 0); }
    void WriteString(const std::string& s) {
        WriteU32((uint32_t)s.size());
        Append(s.data(), s.size());
    }
    void WriteBytes(const void* p, size_t n) {
        WriteU32((uint32_t)n);
        Append(p, n);
    }

    // Deserialization (must be constructed with packed data).
    explicit Serializer(const void* data, size_t n) : data_(static_cast<const char*>(data)), size_(n) {}
    uint8_t ReadByte() { return (uint8_t)data_[pos_++]; }
    int32_t ReadI32() { int32_t v; Peek(&v, sizeof(v)); return v; }
    uint32_t ReadU32() { uint32_t v; Peek(&v, sizeof(v)); return v; }
    float ReadF32() { float v; Peek(&v, sizeof(v)); return v; }
    double ReadF64() { double v; Peek(&v, sizeof(v)); return v; }
    bool ReadBool() { return ReadByte() != 0; }
    std::string ReadString() {
        uint32_t n = ReadU32();
        std::string s(data_ + pos_, n);
        pos_ += n;
        return s;
    }
    void ReadBytes(void* out, size_t n) {
        uint32_t len = ReadU32();
        if (len > n) len = n;
        std::memcpy(out, data_ + pos_, len);
        pos_ += len;
    }
    size_t Remaining() const { return size_ - pos_; }

private:
    void Append(const void* p, size_t n) {
        const char* c = static_cast<const char*>(p);
        bytes_.insert(bytes_.end(), c, c + n);
    }
    void Peek(void* out, size_t n) {
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
    }

    std::vector<char> bytes_;
    size_t pos_ = 0;
    const char* data_ = nullptr;
    size_t size_ = 0;
};

} // namespace bighero
