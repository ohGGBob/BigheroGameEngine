#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstddef>
#include <memory>
#include "ShaderProgram.h"

namespace bighero {

// Central store of named shader programs with shared ownership. Building
// block for a runtime shader registry (loads and caches compiled programs).
class ShaderLibrary {
public:
    using ShaderPtr = std::shared_ptr<ShaderProgram>;

    void Add(const std::string& name, const ShaderPtr& shader) {
        shaders_[name] = shader;
    }
    ShaderPtr GetShader(const std::string& name) const {
        auto it = shaders_.find(name);
        return it != shaders_.end() ? it->second : nullptr;
    }
    bool Has(const std::string& name) const { return shaders_.count(name) != 0; }
    std::size_t Count() const { return shaders_.size(); }
    bool Empty() const { return shaders_.empty(); }

    ShaderPtr Create(const std::string& name) {
        auto s = std::make_shared<ShaderProgram>(name);
        shaders_[name] = s;
        return s;
    }

    bool Remove(const std::string& name) { return shaders_.erase(name) > 0; }
    void Clear() { shaders_.clear(); }

    std::vector<std::string> Names() const {
        std::vector<std::string> out;
        for (const auto& kv : shaders_) out.push_back(kv.first);
        return out;
    }

private:
    std::map<std::string, ShaderPtr> shaders_;
};

} // namespace bighero
