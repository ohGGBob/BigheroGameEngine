#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <typeindex>

namespace bighero {

// ComponentRegistry: registers component type ids for an ECS and provides
// id<->name lookup. Self-contained, std-lib only.
class ComponentRegistry {
public:
    ComponentRegistry() = default;

    // Register a component type by name and deduce a stable id from the type.
    template <class T>
    uint64_t Register(const char* name) {
        uint64_t id = ComputeId(name);
        if (byId_.find(id) == byId_.end()) byId_[id] = name ? name : "Component";
        return id;
    }
    uint64_t Register(uint64_t id, const char* name) {
        if (byId_.find(id) == byId_.end()) byId_[id] = name ? name : "Component";
        return id;
    }

    bool HasComponent(uint64_t id) const { return byId_.find(id) != byId_.end(); }
    const std::string& NameOf(uint64_t id) const {
        static const std::string empty;
        auto it = byId_.find(id);
        return it == byId_.end() ? empty : it->second;
    }
    size_t Count() const { return byId_.size(); }
    void Clear() { byId_.clear(); }

    static uint64_t ComputeId(const char* name) {
        if (!name) return 0;
        uint64_t h = 0xcbf29ce484222325ull;
        while (*name) { h ^= (uint8_t)*name++; h *= 0x100000001b3ull; }
        return h ? h : 1;
    }

private:
    std::unordered_map<uint64_t, std::string> byId_;
};

} // namespace bighero
