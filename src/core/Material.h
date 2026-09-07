#pragma once
#include <string>
#include <vector>
#include <map>

namespace bighero {

// Render material: shader reference + named uniform/scalar/vector params.
class Material {
public:
    explicit Material(const std::string& name = "") : name_(name) {}

    // Scalar float uniform.
    void SetFloat(const std::string& name, float v) { floats_[name] = v; }
    float GetFloat(const std::string& name, float def = 0) const {
        auto it = floats_.find(name);
        return it != floats_.end() ? it->second : def;
    }

    // Integer uniform (e.g. texture slot).
    void SetInt(const std::string& name, int v) { ints_[name] = v; }
    int GetInt(const std::string& name, int def = 0) const {
        auto it = ints_.find(name);
        return it != ints_.end() ? it->second : def;
    }

    // Tag string (e.g. shader name / technique).
    void SetTag(const std::string& name, const std::string& v) { tags_[name] = v; }
    std::string GetTag(const std::string& name, const std::string& def = "") const {
        auto it = tags_.find(name);
        return it != tags_.end() ? it->second : def;
    }

    void SetProgram(int p) { program_ = p; }
    int Program() const { return program_; }
    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }

    std::size_t FloatCount() const { return floats_.size(); }
    bool Empty() const { return floats_.empty() && ints_.empty() && tags_.empty(); }

private:
    std::string name_;
    int program_ = 0;
    std::map<std::string, float> floats_;
    std::map<std::string, int> ints_;
    std::map<std::string, std::string> tags_;
};

} // namespace bighero
