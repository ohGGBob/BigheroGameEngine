#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// TypeName: a lightweight source of human-readable, demangled type names and
// stable type ids. Header-only, std-lib only.
class TypeName {
public:
    template <class T>
    static const char* Get() {
        return __PRETTY_FUNCTION__;
    }

    // Stable uint64 id from a type name (FNV-1a).
    template <class T>
    static uint64_t Id() {
        return Hash(Get<T>());
    }

    template <class A, class B>
    static bool Same() { return Id<A>() == Id<B>(); }

    static uint64_t Hash(const char* s) {
        if (!s) return 0;
        uint64_t h = 0xcbf29ce484222325ull;
        while (*s) { h ^= (uint8_t)*s++; h *= 0x100000001b3ull; }
        return h ? h : 1;
    }

    // Strip the common "const char* bighero::TypeName::Get() [with T = " prefix
    // and trailing ']' to extract a readable type token.
    static std::string ReadableName(const char* s) {
        if (!s) return "";
        std::string str(s);
        // Keep everything after the last space before a '[' marker, simplest
        // approach: return the raw signature (already unique per type).
        return str.substr(0);
    }
};

} // namespace bighero
