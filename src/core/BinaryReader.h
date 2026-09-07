#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace bighero {

// Minimal little-endian binary reader over a byte buffer.
class BinaryReader {
public:
    BinaryReader(const uint8_t* data, std::size_t size)
        : data_(data), size_(size), pos_(0) {}
    explicit BinaryReader(const std::vector<uint8_t>& v)
        : data_(v.data()), size_(v.size()), pos_(0) {}

    bool ReadU8(uint8_t& v)   { return ReadRaw(&v, 1); }
    bool ReadI8(int8_t& v)    { return ReadRaw(&v, 1); }
    bool ReadU16(uint16_t& v) { return ReadRaw(&v, 2); }
    bool ReadI16(int16_t& v)  { return ReadRaw(&v, 2); }
    bool ReadU32(uint32_t& v) { return ReadRaw(&v, 4); }
    bool ReadI32(int32_t& v)  { return ReadRaw(&v, 4); }
    bool ReadU64(uint64_t& v) { return ReadRaw(&v, 8); }
    bool ReadI64(int64_t& v)  { return ReadRaw(&v, 8); }
    bool ReadF32(float& v)    { return ReadRaw(&v, 4); }
    bool ReadF64(double& v)   { return ReadRaw(&v, 8); }
    bool ReadBool(bool& v)    { uint8_t b; if (!ReadU8(b)) return false; v = (b != 0); return true; }

    bool ReadBytes(std::vector<uint8_t>& out, std::size_t n) {
        if (pos_ + n > size_) return false;
        out.assign(data_ + pos_, data_ + pos_ + n);
        pos_ += n;
        return true;
    }
    bool Skip(std::size_t n) {
        if (pos_ + n > size_) return false;
        pos_ += n;
        return true;
    }
    std::size_t Position() const { return pos_; }
    std::size_t Size() const { return size_; }
    bool Eof() const { return pos_ >= size_; }

private:
    bool ReadRaw(void* out, std::size_t n) {
        if (pos_ + n > size_) return false;
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
        return true;
    }
    const uint8_t* data_;
    std::size_t size_, pos_;
};

} // namespace bighero
