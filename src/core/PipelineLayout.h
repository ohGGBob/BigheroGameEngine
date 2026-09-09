#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// PipelineLayout: the layout of descriptor sets and push constants for a
// graphics/compute pipeline. Self-contained, std-lib only.
class PipelineLayout {
public:
    struct PushConstant {
        uint32_t offset = 0;
        uint32_t size = 0;
        uint16_t stageFlags = 0;
    };

    PipelineLayout() = default;

    void AddDescriptorSetLayout(uint32_t setIndex) { setLayouts_.push_back(setIndex); }
    size_t DescriptorSetCount() const { return setLayouts_.size(); }
    uint32_t DescriptorSetAt(size_t i) const { return setLayouts_[i]; }
    void ClearDescriptorSets() { setLayouts_.clear(); }

    void AddPushConstant(uint32_t offset, uint32_t size, uint16_t stageFlags = 0x1) {
        PushConstant pc; pc.offset = offset; pc.size = size; pc.stageFlags = stageFlags;
        pushConstants_.push_back(pc);
    }
    size_t PushConstantCount() const { return pushConstants_.size(); }
    const PushConstant& PushConstantAt(size_t i) const { return pushConstants_[i]; }

    uint32_t TotalPushConstantBytes() const {
        uint32_t n = 0;
        for (auto& pc : pushConstants_) n += pc.size;
        return n;
    }
    bool IsEmpty() const { return setLayouts_.empty() && pushConstants_.empty(); }

private:
    std::vector<uint32_t> setLayouts_;
    std::vector<PushConstant> pushConstants_;
};

} // namespace bighero
