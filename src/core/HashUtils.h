#pragma once
// 哈希工具集（HashUtils）：纯标准库、仅头文件。
// 提供 FNV-1a 32/64 位哈希、MurmurHash3 32 位（用于资源名/字符串 ID 的稳定哈希）。
//
// 商业化价值：资源加载时的"字符串->ID"查找、去重、哈希表散列、一致性校验，
// 商业引擎常用稳定且快速的 FNV/Murmur 代替 std::hash（跨平台稳定）。
//
// FNV-1a 是公开域算法；MurmurHash3 由 Austin Appleby 发布在公有领域。

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace BigHero::Core
{
namespace hash
{
// FNV-1a 32 位。
inline uint32_t Fnv1a32(const void* data, size_t len, uint32_t seed = 2166136261u) noexcept
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    uint32_t h = seed;
    for (size_t i = 0; i < len; ++i)
    {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}
inline uint32_t Fnv1a32(std::string_view s, uint32_t seed = 2166136261u) noexcept
{
    return Fnv1a32(s.data(), s.size(), seed);
}

// FNV-1a 64 位。
inline uint64_t Fnv1a64(const void* data, size_t len, uint64_t seed = 1469598103934665603ULL) noexcept
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    uint64_t h = seed;
    for (size_t i = 0; i < len; ++i)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}
inline uint64_t Fnv1a64(std::string_view s, uint64_t seed = 1469598103934665603ULL) noexcept
{
    return Fnv1a64(s.data(), s.size(), seed);
}

// MurmurHash3 x86 32 位（种子可选）。
inline uint32_t Murmur3(const void* data, size_t len, uint32_t seed = 0) noexcept
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    const size_t nblocks = len / 4;
    uint32_t h1 = seed;

    const uint32_t c1 = 0xcc9e2d51u;
    const uint32_t c2 = 0x1b873593u;

    // body
    const uint32_t* blocks = reinterpret_cast<const uint32_t*>(p);
    for (size_t i = 0; i < nblocks; ++i)
    {
        uint32_t k1 = blocks[i];
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);
        k1 *= c2;
        h1 ^= k1;
        h1 = (h1 << 13) | (h1 >> 19);
        h1 = h1 * 5 + 0xe6546b64u;
    }

    // tail
    const unsigned char* tail = p + nblocks * 4;
    uint32_t k1 = 0;
    switch (len & 3)
    {
    case 3: k1 ^= static_cast<uint32_t>(tail[2]) << 16; [[fallthrough]];
    case 2: k1 ^= static_cast<uint32_t>(tail[1]) << 8; [[fallthrough]];
    case 1:
        k1 ^= tail[0];
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);
        k1 *= c2;
        h1 ^= k1;
        break;
    }

    // finalization
    h1 ^= static_cast<uint32_t>(len);
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6bu;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35u;
    h1 ^= h1 >> 16;
    return h1;
}
inline uint32_t Murmur3(std::string_view s, uint32_t seed = 0) noexcept
{
    return Murmur3(s.data(), s.size(), seed);
}
} // namespace hash
} // namespace BigHero::Core
