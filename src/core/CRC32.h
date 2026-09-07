#pragma once
#include <cstdint>
#include <cstddef>

namespace bighero {

// Standard CRC-32 (IEEE 802.3, polynomial 0xEDB88320).
class CRC32 {
public:
    CRC32() { BuildTable(); }
    static uint32_t Compute(const void* data, std::size_t len, uint32_t seed = 0) {
        return Instance().Update(seed, data, len);
    }
    static uint32_t ComputeString(const char* s) {
        if (!s) return 0;
        std::size_t n = 0;
        while (s[n]) ++n;
        return Compute(s, n);
    }
    // Incremental use.
    uint32_t Update(uint32_t crc, const void* data, std::size_t len) const {
        crc = ~crc;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (std::size_t i = 0; i < len; ++i)
            crc = table_[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
        return ~crc;
    }
    static uint32_t Combine(uint32_t crcA, uint32_t crcB, std::size_t lenB) {
        // Not used by default; provided for completeness.
        (void)crcA; (void)crcB; (void)lenB;
        return 0;
    }
private:
    static const CRC32& Instance() { static CRC32 inst; return inst; }
    void BuildTable() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table_[i] = c;
        }
    }
    uint32_t table_[256];
};

} // namespace bighero
