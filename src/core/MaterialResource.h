#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// MaterialResource: describes a material's shader + uniform parameters.
// Self-contained / std-lib only.
class MaterialResource {
public:
    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }
    void SetShaderName(std::string s) { shader_ = std::move(s); }
    const std::string& ShaderName() const { return shader_; }
    void SetShaderIndex(uint32_t i) { shaderIndex_ = i; }
    uint32_t ShaderIndex() const { return shaderIndex_; }

    // named float parameter
    void SetFloat(const std::string& name, float v) {
        for (auto& p : floats_) if (p.first == name) { p.second = v; return; }
        floats_.emplace_back(name, v);
    }
    bool GetFloat(const std::string& name, float& out) const {
        for (auto& p : floats_) if (p.first == name) { out = p.second; return true; }
        return false;
    }
    void ClearFloats() { floats_.clear(); }
    const std::vector<std::pair<std::string,float>>& Floats() const { return floats_; }
    size_t FloatCount() const { return floats_.size(); }

    void SetTransparent(bool b) { transparent_ = b; }
    bool Transparent() const { return transparent_; }
    void SetDoubleSided(bool b) { doubleSided_ = b; }
    bool DoubleSided() const { return doubleSided_; }

    bool IsValid() const { return !shader_.empty(); }

private:
    std::string name_, shader_;
    uint32_t shaderIndex_ = 0xFFFFFFFFu;
    std::vector<std::pair<std::string,float>> floats_;
    bool transparent_ = false, doubleSided_ = false;
};

} // namespace bighero
