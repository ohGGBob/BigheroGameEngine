#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// Hasher: collection of fast, well-distributed hash functions (FNV-1a, djb2,
// Murmur3 finalizer) usable for strings, buffers and 32/64-bit ints.
// Self-contained, std-lib only.
class Hasher {
public:
    static uint32_t Fnv1a32(const char* s) {
        uint32_t h = 0x811c9dc5u;
        if (!s) return h;
        while (*s) { h ^= (uint8_t)*s++; h *= 0x01000193u; }
        return h;
    }
    static uint64_t Fnv1a64(const char* s) {
        uint64_t h = 0xcbf29ce484222325ull;
        if (!s) return h;
        while (*s) { h ^= (uint8_t)*s++; h *= 0x100000001b3ull; }
        return h;
    }
    static uint32_t Djb2(const char* s) {
        uint32_t h = 5381;
        if (!s) return h;
        while (*s) { h = ((h << 5) + h) + (uint8_t)*s++; }
        return h;
    }
    static uint64_t Fnv1a64(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        uint64_t h = 0xcbf29ce484222325ull;
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001b3ull; }
        return h;
    }
    // Murmur3 finalizer (well-distributed 32-bit avalanche).
    static uint32_t Murmur3(uint32_t h) {
        h ^= h >> 16; h *= 0x85ebca6bu;
        h ^= h >> 13; h *= 0xc2b2ae35u;
        h ^= h >> 16;
        return h;
    }
    static uint32_t Combine32(uint32_t a, uint32_t b) { return Murmur3(a ^ (b * 0x9e3779b9u)); }
    static uint64_t Combine64(uint64_t a, uint64_t b) {
        a ^= b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2);
        return a;
    }
};

} // namespace bighero
