#pragma once
#include <string>
#include <vector>

namespace bighero {

// Shader: a program handle plus named passes / attribute and uniform names.
// This is a CPU-side reflection/metadata handle; actual GPU compilation is
// downstream of the renderer.
class Shader {
public:
    enum class Stage { Vertex, Fragment, Compute, Geometry };

    explicit Shader(const std::string& name = "") : name_(name) {}

    void SetName(const std::string& n) { name_ = n; }
    const std::string& Name() const { return name_; }
    void SetProgram(int p) { program_ = p; }
    int Program() const { return program_; }

    // Load source for a stage; last source for a stage wins.
    void SetSource(Stage s, const std::string& src) {
        if ((int)s >= (int)sources_.size()) sources_.resize((int)s + 1);
        sources_[(int)s] = src;
    }
    const std::string& Source(Stage s) const {
        static const std::string empty;
        if ((int)s >= (int)sources_.size()) return empty;
        return sources_[(int)s];
    }

    void AddAttribute(const std::string& a) { attributes_.push_back(a); }
    void AddUniform(const std::string& u) { uniforms_.push_back(u); }
    std::size_t AttributeCount() const { return attributes_.size(); }
    std::size_t UniformCount() const { return uniforms_.size(); }
    const std::vector<std::string>& Attributes() const { return attributes_; }
    const std::vector<std::string>& Uniforms() const { return uniforms_; }

    bool IsValid() const { return program_ != 0; }

private:
    std::string name_;
    int program_ = 0;
    std::vector<std::string> sources_;   // indexed by Stage
    std::vector<std::string> attributes_;
    std::vector<std::string> uniforms_;
};

} // namespace bighero
