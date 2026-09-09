#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace bighero {

// TypeRegistry: a global-ish registry mapping stable type ids to TypeInfo.
// Ids are derived from an FNV-1a hash of the type name, so lookups are
// consistent across translation units. Self-contained, std-lib only.
class TypeRegistry {
public:
    TypeRegistry() = default;

    static uint64_t ComputeTypeId(const char* name) {
        if (!name) return 0;
        uint64_t h = 0xcbf29ce484222325ull;
        while (*name) { h ^= (uint8_t)*name++; h *= 0x100000001b3ull; }
        return h ? h : 1;
    }

    // Register a type; returns the stored TypeInfo reference.
    const TypeInfo& Register(uint64_t id, const char* name, size_t size) {
        auto it = types_.find(id);
        if (it != types_.end()) return it->second;
        auto res = types_.emplace(id, TypeInfo(id, name, size));
        return res.first->second;
    }
    template <class T>
    const TypeInfo& RegisterType(const char* name) {
        uint64_t id = ComputeTypeId(name);
        return Register(id, name, sizeof(T));
    }

    const TypeInfo* Find(uint64_t id) const {
        auto it = types_.find(id);
        return it == types_.end() ? nullptr : &it->second;
    }
    const TypeInfo* FindByName(const char* name) const {
        return Find(ComputeTypeId(name));
    }
    bool Contains(uint64_t id) const { return types_.find(id) != types_.end(); }
    size_t Count() const { return types_.size(); }
    void Clear() { types_.clear(); }

private:
    std::unordered_map<uint64_t, TypeInfo> types_;
};

} // namespace bighero
