#pragma once
#include <cstdint>
#include <atomic>

namespace bighero {

// Compile-time unique type id registry (stable across calls for a given T).
class TypeIndex {
public:
    template <typename T>
    static std::uint32_t Get() {
        static const std::uint32_t id = NextId();
        return id;
    }

    // Runtime (type-erased) id from a name hash, stable per string.
    static std::uint32_t FromName(const char* name) {
        std::uint32_t h = 2166136261u;
        for (const char* p = name; *p; ++p) {
            h ^= (unsigned char)*p;
            h *= 16777619u;
        }
        return h | 0x80000000u; // mark hashed ids distinctly
    }

private:
    static std::uint32_t NextId() {
        static std::atomic<std::uint32_t> counter{0};
        return counter.fetch_add(1) + 1;
    }
};

} // namespace bighero
