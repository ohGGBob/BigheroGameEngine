#pragma once
#include <string>
#include <map>
#include <vector>
#include <array>
#include <cstddef>
#include <cstdint>

namespace bighero {

// MaterialPropertyBlock: a set of per-material overrides (floats, vectors,
// colors, textures) layered on top of a Material. Backends upload these as
// uniform/texture overrides per draw. Pure data container.
class MaterialPropertyBlock {
public:
    MaterialPropertyBlock() {}

    void SetFloat(const std::string& name, float v) { floats_[name] = v; }
    float GetFloat(const std::string& name, float def = 0.0f) const {
        auto it = floats_.find(name); return it == floats_.end() ? def : it->second;
    }
    void SetVec4(const std::string& name, float x, float y, float z, float w) {
        float v[4] = {x,y,z,w};
        for (int i = 0; i < 4; ++i) vec4_[name][i] = v[i];
    }
    bool GetVec4(const std::string& name, float out[4]) const {
        auto it = vec4_.find(name);
        if (it == vec4_.end()) return false;
        for (int i = 0; i < 4; ++i) out[i] = it->second[i];
        return true;
    }
    void SetColor(const std::string& name, float r, float g, float b, float a = 1.0f) {
        color_[name] = {r,g,b,a};
    }
    void SetTexture(const std::string& name, std::uint64_t tex) { tex_[name] = tex; }
    std::uint64_t GetTexture(const std::string& name, std::uint64_t def = 0) const {
        auto it = tex_.find(name); return it == tex_.end() ? def : it->second;
    }
    void SetKeyword(const std::string& k, bool on) { keywords_[k] = on; }
    bool HasKeyword(const std::string& k) const {
        auto it = keywords_.find(k); return it != keywords_.end() && it->second;
    }

    void Clear() { floats_.clear(); vec4_.clear(); color_.clear(); tex_.clear(); keywords_.clear(); }
    std::size_t FloatCount() const { return floats_.size(); }
    std::size_t TextureCount() const { return tex_.size(); }

private:
    std::map<std::string, float> floats_;
    std::map<std::string, std::array<float,4>> vec4_;
    std::map<std::string, std::array<float,4>> color_;
    std::map<std::string, std::uint64_t> tex_;
    std::map<std::string, bool> keywords_;
};

} // namespace bighero
