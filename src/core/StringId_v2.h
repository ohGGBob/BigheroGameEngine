#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace bighero {

// StringId: interned string-to-id map for comparing/PCH-friendly identifiers.
// Returns stable, collision-resistant 64-bit ids derived from content hash.
// Self-contained, std-lib only.
class StringId {
public:
    StringId() = default;

    // Compute the id for a string without storing it.
    static uint64_t Compute(const char* s) {
        if (!s) return 0;
        uint64_t h = 0xcbf29ce484222325ull;
        while (*s) {
            h ^= (uint8_t)*s++;
            h *= 0x100000001b3ull;
        }
        return h;
    }
    static uint64_t Compute(const std::string& s) { return Compute(s.c_str()); }

    // Intern: return the id, storing the string if not seen before.
    uint64_t Intern(const char* s) {
        uint64_t id = Compute(s);
        if (store_.find(id) == store_.end()) {
            store_[id] = s ? s : "";
        }
        return id;
    }
    uint64_t Intern(const std::string& s) { return Intern(s.c_str()); }

    // Look up the original string for an id ("" if unknown).
    const std::string& Lookup(uint64_t id) const {
        static const std::string empty;
        auto it = store_.find(id);
        return it == store_.end() ? empty : it->second;
    }

    bool Contains(uint64_t id) const { return store_.find(id) != store_.end(); }
    size_t Size() const { return store_.size(); }
    void Clear() { store_.clear(); }

    static constexpr uint64_t None = 0;

private:
    std::unordered_map<uint64_t, std::string> store_;
};

} // namespace bighero
