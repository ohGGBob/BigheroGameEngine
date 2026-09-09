#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace bighero {

// ShaderResourceDescriptorSet: a collection of shader resource bindings
// (uniform buffers, samplers, textures) grouped into a descriptor set.
// Self-contained, std-lib only.
class ShaderResourceDescriptorSet {
public:
    struct Binding {
        uint32_t binding = 0;
        uint32_t set = 0;          // descriptor set index
        uint16_t type = 0;         // 0=uniform buffer, 1=combined sampler, 2=storage buffer, 3=texture
        uint32_t count = 1;        // array size
        uint16_t stageFlags = 0;   // bitmask of shader stages
    };

    ShaderResourceDescriptorSet() = default;
    explicit ShaderResourceDescriptorSet(uint32_t setIndex) : setIndex_(setIndex) {}

    void SetSetIndex(uint32_t s) { setIndex_ = s; }
    uint32_t SetIndex() const { return setIndex_; }

    void AddBinding(uint32_t binding, uint16_t type, uint32_t count = 1, uint16_t stageFlags = 0x1) {
        Binding b; b.binding = binding; b.set = setIndex_; b.type = type; b.count = count; b.stageFlags = stageFlags;
        bindings_.push_back(b);
    }
    size_t BindingCount() const { return bindings_.size(); }
    const Binding& BindingAt(size_t i) const { return bindings_[i]; }
    bool IsEmpty() const { return bindings_.empty(); }
    void RemoveAll() { bindings_.clear(); }

    uint32_t TotalBindings() const {
        uint32_t n = 0;
        for (auto& b : bindings_) n += b.count;
        return n;
    }

    static const char* TypeName(uint16_t t) {
        switch (t) {
            case 0: return "UniformBuffer";
            case 1: return "CombinedSampler";
            case 2: return "StorageBuffer";
            case 3: return "Texture";
            default: return "Other";
        }
    }

private:
    uint32_t setIndex_ = 0;
    std::vector<Binding> bindings_;
};

} // namespace bighero
