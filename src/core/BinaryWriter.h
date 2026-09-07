#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace bighero {

// Minimal little-endian binary writer appending to a byte vector.
class BinaryWriter {
public:
    void WriteU8(uint8_t v)  { Append(&v, 1); }
    void WriteI8(int8_t v)   { Append(&v, 1); }
    void WriteU16(uint16_t v){ Append(&v, 2); }
    void WriteI16(int16_t v) { Append(&v, 2); }
    void WriteU32(uint32_t v){ Append(&v, 4); }
    void WriteI32(int32_t v) { Append(&v, 4); }
    void WriteU64(uint64_t v){ Append(&v, 8); }
    void WriteI64(int64_t v) { Append(&v, 8); }
    void WriteF32(float v)   { Append(&v, 4); }
    void WriteF64(double v)  { Append(&v, 8); }
    void WriteBool(bool v)   { uint8_t b = v ? 1 : 0; Append(&b, 1); }
    void WriteBytes(const uint8_t* p, std::size_t n) {
        if (p) buf_.insert(buf_.end(), p, p + n);
    }
    void WriteBytes(const std::vector<uint8_t>& v) { WriteBytes(v.data(), v.size()); }

    const std::vector<uint8_t>& Data() const { return buf_; }
    std::vector<uint8_t>& Data() { return buf_; }
    std::size_t Size() const { return buf_.size(); }
    void Clear() { buf_.clear(); }

private:
    void Append(const void* p, std::size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    std::vector<uint8_t> buf_;
};

} // namespace bighero
