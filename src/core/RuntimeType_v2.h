#pragma once
#include <cstdint>

#if defined(_MSC_VER)
#define BH_PRETTY_FUNCTION __FUNCSIG__
#else
#define BH_PRETTY_FUNCTION __PRETTY_FUNCTION__
#endif

namespace bighero {

// RuntimeType: compile-time type identity helpers (RTTI wrapper for stable
// hashed type ids). Provides TypeId<T>() for any type without needing std::type_info.
// Self-contained, std-lib only.
class RuntimeType {
public:
    template <class T>
    static uint64_t TypeId() {
        static const uint64_t id = Compute(GetStaticName<T>());
        return id;
    }

    template <class T>
    static const char* Name() { return GetStaticName<T>(); }

    static uint64_t Compute(const char* name) {
        if (!name) return 0;
        uint64_t h = 0xcbf29ce484222325ull;
        while (*name) { h ^= (uint8_t)*name++; h *= 0x100000001b3ull; }
        return h ? h : 1;
    }

    // True if two types share the same id.
    template <class A, class B>
    static bool IsSame() { return TypeId<A>() == TypeId<B>(); }

private:
    // Use the compiler's function-signature builtin to get a type-stable name
    // string. This collapses to the unspecialized template for most compilers
    // but is fine as a unique per-type tag for id derivation.
    template <class T>
    static const char* GetStaticName() {
        return BH_PRETTY_FUNCTION;
    }
};

} // namespace bighero
