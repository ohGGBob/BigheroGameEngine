#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace bighero {

// Metadata: an ordered-ish key/value string store used for object metadata,
// tags, and arbitrary annotations. Self-contained, std-lib only.
class Metadata {
public:
    Metadata() = default;

    void Set(const std::string& key, const std::string& value) { map_[key] = value; }
    void Set(const char* key, const char* value) { if (key && value) map_[key] = value; }
    void SetInt(const std::string& key, int64_t v) { map_[key] = std::to_string(v); }
    void SetFloat(const std::string& key, double v) { map_[key] = std::to_string(v); }
    void SetBool(const std::string& key, bool v) { map_[key] = v ? "true" : "false"; }

    bool Has(const std::string& key) const { return map_.find(key) != map_.end(); }
    bool Get(const std::string& key, std::string& out) const {
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        out = it->second;
        return true;
    }
    std::string Get(const std::string& key, const std::string& def = "") const {
        auto it = map_.find(key);
        return it == map_.end() ? def : it->second;
    }
    int64_t GetInt(const std::string& key, int64_t def = 0) const {
        auto it = map_.find(key);
        if (it == map_.end()) return def;
        try { return std::stoll(it->second); } catch (...) { return def; }
    }
    double GetFloat(const std::string& key, double def = 0.0) const {
        auto it = map_.find(key);
        if (it == map_.end()) return def;
        try { return std::stod(it->second); } catch (...) { return def; }
    }
    bool GetBool(const std::string& key, bool def = false) const {
        auto it = map_.find(key);
        if (it == map_.end()) return def;
        return it->second == "true" || it->second == "1";
    }

    void Erase(const std::string& key) { map_.erase(key); }
    bool Empty() const { return map_.empty(); }
    size_t Size() const { return map_.size(); }
    void Clear() { map_.clear(); }

    bool ContainsTag(const std::string& tag) const { return Has(tag) && Get(tag) == "true"; }
    void AddTag(const std::string& tag) { map_[tag] = "true"; }

private:
    std::unordered_map<std::string, std::string> map_;
};

} // namespace bighero
