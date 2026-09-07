#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstddef>
#include <memory>
#include "Material.h"

namespace bighero {

// Central store of named materials with reference-counted shared ownership.
// Building block for a runtime material registry.
class MaterialLibrary {
public:
    using MaterialPtr = std::shared_ptr<Material>;

    void SetMaterial(const std::string& name, const MaterialPtr& mat) {
        materials_[name] = mat;
    }
    MaterialPtr GetMaterial(const std::string& name) const {
        auto it = materials_.find(name);
        return it != materials_.end() ? it->second : nullptr;
    }
    bool Has(const std::string& name) const { return materials_.count(name) != 0; }
    std::size_t Count() const { return materials_.size(); }
    bool Empty() const { return materials_.empty(); }

    // Create-and-register a new material; returns the shared pointer.
    MaterialPtr Create(const std::string& name, int program = 0) {
        auto m = std::make_shared<Material>(name);
        if (program) m->SetProgram(program);
        materials_[name] = m;
        return m;
    }

    // Remove a material by name; returns true if it was present.
    bool Remove(const std::string& name) { return materials_.erase(name) > 0; }
    void Clear() { materials_.clear(); }

    std::vector<std::string> Names() const {
        std::vector<std::string> out;
        for (const auto& kv : materials_) out.push_back(kv.first);
        return out;
    }

private:
    std::map<std::string, MaterialPtr> materials_;
};

} // namespace bighero
