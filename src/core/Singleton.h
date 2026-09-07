#pragma once
#include <cstddef>

namespace bighero {

// Minimal thread-unsafe singleton helper (used sparingly for engine services).
template <typename T>
class Singleton {
public:
    static T& Get() {
        static T inst;
        return inst;
    }
    static T* Ptr() { return &Get(); }
    static bool Exists() { static bool ok = true; return ok; }

    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;
};

} // namespace bighero
