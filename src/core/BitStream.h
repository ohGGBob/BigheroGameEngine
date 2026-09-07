#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace bighero {

// Little-endian bit stream writer/reader over a growable byte buffer.
class BitStream {
public:
    // --- Writer ---
    void WriteBits(std::uint32_t value, int bitCount) {
        for (int i = 0; i < bitCount; ++i) {
            if (bytePos_ >= buffer_.size()) buffer_.push_back(0);
            if ((value >> i) & 1u) buffer_[bytePos_] |= (std::uint8_t)(1u << bitPos_);
            bitPos_++;
            if (bitPos_ == 8) { bitPos_ = 0; bytePos_++; }
        }
    }

    void WriteByte(std::uint8_t v) { WriteBits(v, 8); }

    void WriteU32(std::uint32_t v) { WriteBits(v, 32); }

    void WriteFloat(float v) {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        WriteBits(bits, 32);
    }

    // --- Reader ---
    std::uint32_t ReadBits(int bitCount) {
        std::uint32_t value = 0;
        for (int i = 0; i < bitCount; ++i) {
            bool bit = false;
            if (bytePos_ < buffer_.size())
                bit = (buffer_[bytePos_] >> bitPos_) & 1u;
            if (bit) value |= (1u << i);
            bitPos_++;
            if (bitPos_ == 8) { bitPos_ = 0; bytePos_++; }
        }
        return value;
    }

    std::uint8_t ReadByte() { return (std::uint8_t)ReadBits(8); }

    std::uint32_t ReadU32() { return ReadBits(32); }

    float ReadFloat() {
        std::uint32_t bits = ReadBits(32);
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    // Reset for reading from the current buffer from the beginning.
    void ResetRead() { bytePos_ = 0; bitPos_ = 0; }

    // Reset everything (clear buffer and positions).
    void Reset() { buffer_.clear(); bytePos_ = 0; bitPos_ = 0; }

    const std::vector<std::uint8_t>& Data() const { return buffer_; }
    std::size_t Size() const { return buffer_.size(); }

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t bytePos_ = 0;
    int bitPos_ = 0;
};

} // namespace bighero
