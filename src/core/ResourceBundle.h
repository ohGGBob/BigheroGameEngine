#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstddef>
#include <cstdint>

namespace bighero {

// ResourceBundle: a named registry of typed resources (id, type tag, data
// blob) that tracks load state and dependencies. Pure stdlib container.
class ResourceBundle {
public:
    enum class Status { Unloaded, Loading, Loaded, Failed };

    struct Entry {
        std::string id;
        std::string type;      // "texture", "mesh", "audio", ...
        std::string path;
        std::vector<std::string> deps;   // dependency ids
        Status status = Status::Unloaded;
        std::uint64_t handle = 0;
    };

    ResourceBundle() {}

    std::size_t Add(const std::string& id, const std::string& type,
                    const std::string& path) {
        Entry e; e.id = id; e.type = type; e.path = path;
        entries_[id] = e;
        return entries_.size();
    }
    bool Remove(const std::string& id) { return entries_.erase(id) != 0; }
    std::size_t Count() const { return entries_.size(); }

    bool Get(const std::string& id, Entry& out) const {
        auto it = entries_.find(id);
        if (it == entries_.end()) return false;
        out = it->second; return true;
    }
    void SetStatus(const std::string& id, Status s) {
        auto it = entries_.find(id);
        if (it != entries_.end()) it->second.status = s;
    }
    Status StatusOf(const std::string& id) const {
        auto it = entries_.find(id);
        return it == entries_.end() ? Status::Unloaded : it->second.status;
    }
    void SetHandle(const std::string& id, std::uint64_t h) {
        auto it = entries_.find(id);
        if (it != entries_.end()) it->second.handle = h;
    }

    void AddDependency(const std::string& id, const std::string& depId) {
        auto it = entries_.find(id);
        if (it != entries_.end()) it->second.deps.push_back(depId);
    }
    std::size_t DependencyCount(const std::string& id) const {
        auto it = entries_.find(id);
        return it == entries_.end() ? 0 : it->second.deps.size();
    }
    bool GetDependency(const std::string& id, std::size_t i, std::string& out) const {
        auto it = entries_.find(id);
        if (it == entries_.end() || i >= it->second.deps.size()) return false;
        out = it->second.deps[i]; return true;
    }

    std::size_t CountByStatus(Status s) const {
        std::size_t n = 0;
        for (auto& kv : entries_) if (kv.second.status == s) ++n;
        return n;
    }

private:
    std::map<std::string, Entry> entries_;
};

} // namespace bighero
