#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// DescriptorSetLayout: describes the layout of a descriptor set (bindings,
// types, stage usage). Self-contained, std-lib only.
class DescriptorSetLayout {
public:
    struct LayoutBinding {
        uint32_t binding = 0;
        uint16_t type = 0;          // 0=uniform buffer, 1=combined sampler, 2=storage buffer, 3=texture
        uint32_t count = 1;         // array size
        uint16_t stageFlags = 0;    // shader stage bitmask
    };

    DescriptorSetLayout() = default;
    explicit DescriptorSetLayout(uint32_t setIndex) : setIndex_(setIndex) {}

    void SetSetIndex(uint32_t s) { setIndex_ = s; }
    uint32_t SetIndex() const { return setIndex_; }

    void AddBinding(uint32_t binding, uint16_t type, uint32_t count = 1, uint16_t stageFlags = 0x1) {
        LayoutBinding b; b.binding = binding; b.type = type; b.count = count; b.stageFlags = stageFlags;
        bindings_.push_back(b);
    }
    size_t BindingCount() const { return bindings_.size(); }
    const LayoutBinding& BindingAt(size_t i) const { return bindings_[i]; }
    void RemoveAll() { bindings_.clear(); }

    uint32_t TotalBindings() const {
        uint32_t n = 0;
        for (auto& b : bindings_) n += b.count;
        return n;
    }
    bool IsEmpty() const { return bindings_.empty(); }

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
    std::vector<LayoutBinding> bindings_;
};

} // namespace bighero
