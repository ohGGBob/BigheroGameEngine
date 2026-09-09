#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// AssetRegistry: an ordered collection of named asset metadata entries with
// lookup helpers. Self-contained / std-lib only.
class AssetRegistry {
public:
    struct Entry {
        std::string name;
        std::string path;
        uint32_t type = 0;    // AssetType as uint8_t
        uint32_t state = 0;   // LoadState as uint8_t
        uint64_t size = 0;
    };

    size_t Count() const { return entries_.size(); }
    bool Empty() const { return entries_.empty(); }

    size_t Add(const Entry& e) { entries_.push_back(e); return entries_.size() - 1; }
    size_t Add(std::string name, std::string path) {
        Entry e; e.name = std::move(name); e.path = std::move(path);
        return Add(e);
    }

    const Entry* Find(const std::string& name) const {
        for (auto& e : entries_) if (e.name == name) return &e;
        return nullptr;
    }
    Entry* Find(const std::string& name) {
        for (auto& e : entries_) if (e.name == name) return &e;
        return nullptr;
    }

    bool Remove(const std::string& name) {
        for (size_t i=0;i<entries_.size();++i)
            if (entries_[i].name == name) { entries_.erase(entries_.begin()+ (intptr_t)i); return true; }
        return false;
    }
    void Clear() { entries_.clear(); }

    std::vector<Entry> ByState(uint32_t state) const {
        std::vector<Entry> out;
        for (auto& e : entries_) if (e.state == state) out.push_back(e);
        return out;
    }
    const std::vector<Entry>& Entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

} // namespace bighero
