#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bighero {

// TagManager: maps named tags (e.g. "Player","Enemy") to stable integer ids and
// back. Self-contained, std-lib only.
class TagManager {
public:
    TagManager() = default;

    // Get or assign a stable id for a tag name.
    int Register(const std::string& name) {
        auto it = byName_.find(name);
        if (it != byName_.end()) return it->second;
        int id = nextId_++;
        byName_[name] = id;
        byId_[id] = name;
        return id;
    }
    int Register(const char* name) { return Register(std::string(name ? name : "")); }

    bool HasTag(const std::string& name) const { return byName_.find(name) != byName_.end(); }
    int Lookup(const std::string& name) const {
        auto it = byName_.find(name);
        return it == byName_.end() ? -1 : it->second;
    }
    std::string NameOf(int id) const {
        auto it = byId_.find(id);
        return it == byId_.end() ? std::string() : it->second;
    }

    size_t Count() const { return byName_.size(); }
    void Clear() { byName_.clear(); byId_.clear(); nextId_ = 0; }

    const std::vector<std::string>& AllTags() const {
        static std::vector<std::string> empty;
        return empty;
    }
    // Populate a list of all registered tag names.
    void CollectTags(std::vector<std::string>& out) const {
        out.clear();
        for (auto& kv : byName_) out.push_back(kv.first);
    }

private:
    std::unordered_map<std::string, int> byName_;
    std::unordered_map<int, std::string> byId_;
    int nextId_ = 0;
};

} // namespace bighero
