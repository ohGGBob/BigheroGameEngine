#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// PlayerPrefs: a simple persistent key/value store with typed getters/setters.
// Values are stored internally (and could be serialized to disk by the backend).
class PlayerPrefs {
public:
    PlayerPrefs() {}

    void SetInt(const std::string& k, int v) { ints_[k] = v; }
    int GetInt(const std::string& k, int def = 0) const {
        auto it = ints_.find(k); return it == ints_.end() ? def : it->second;
    }
    void SetFloat(const std::string& k, float v) { floats_[k] = v; }
    float GetFloat(const std::string& k, float def = 0.0f) const {
        auto it = floats_.find(k); return it == floats_.end() ? def : it->second;
    }
    void SetString(const std::string& k, const std::string& v) { strs_[k] = v; }
    std::string GetString(const std::string& k, const std::string& def = "") const {
        auto it = strs_.find(k); return it == strs_.end() ? def : it->second;
    }
    void SetBool(const std::string& k, bool v) { bools_[k] = v; }
    bool GetBool(const std::string& k, bool def = false) const {
        auto it = bools_.find(k); return it == bools_.end() ? def : it->second;
    }

    bool HasKey(const std::string& k) const {
        return ints_.count(k) || floats_.count(k) || strs_.count(k) || bools_.count(k);
    }
    void DeleteKey(const std::string& k) {
        ints_.erase(k); floats_.erase(k); strs_.erase(k); bools_.erase(k);
    }
    void DeleteAll() { ints_.clear(); floats_.clear(); strs_.clear(); bools_.clear(); }
    std::size_t Count() const { return ints_.size() + floats_.size() + strs_.size() + bools_.size(); }

private:
    std::map<std::string, int> ints_;
    std::map<std::string, float> floats_;
    std::map<std::string, std::string> strs_;
    std::map<std::string, bool> bools_;
};

} // namespace bighero
