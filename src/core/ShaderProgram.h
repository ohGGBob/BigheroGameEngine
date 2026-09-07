#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstddef>

namespace bighero {

// CPU-side shader program handle: links multiple compiled stage shaders into
// a program, and caches uniform/attribute bindings by name.
class ShaderProgram {
public:
    ShaderProgram() {}
    explicit ShaderProgram(const std::string& name) : name_(name) {}

    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }

    // Attach a compiled shader stage (by handle).
    void Attach(int shaderHandle) { shaders_.push_back(shaderHandle); }
    void Link() { linked_ = true; }
    bool Linked() const { return linked_; }
    std::size_t ShaderCount() const { return shaders_.size(); }
    int ShaderAt(std::size_t i) const { return shaders_[i]; }

    void SetProgram(int handle) { program_ = handle; }
    int Program() const { return program_; }

    // Cache a uniform binding location.
    void SetUniformLocation(const std::string& name, int loc) { uniforms_[name] = loc; }
    int GetUniformLocation(const std::string& name, int def = -1) const {
        auto it = uniforms_.find(name);
        return it != uniforms_.end() ? it->second : def;
    }

    // Cache an attribute binding location.
    void SetAttributeLocation(const std::string& name, int loc) { attribs_[name] = loc; }
    int GetAttributeLocation(const std::string& name, int def = -1) const {
        auto it = attribs_.find(name);
        return it != attribs_.end() ? it->second : def;
    }

    bool IsValid() const { return linked_ && program_ != 0; }
    std::size_t UniformCount() const { return uniforms_.size(); }

private:
    std::string name_;
    std::vector<int> shaders_;
    bool linked_ = false;
    int program_ = 0;
    std::map<std::string, int> uniforms_;
    std::map<std::string, int> attribs_;
};

} // namespace bighero
