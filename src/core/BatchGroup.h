#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// BatchGroup: groups renderable items that share a material/property set so
// they can be batched into fewer draw calls. Self-contained, std-lib only.
class BatchGroup {
public:
    BatchGroup() = default;
    BatchGroup(uint64_t material, uint64_t pipeline)
        : material_(material), pipeline_(pipeline) {}

    void SetMaterial(uint64_t m) { material_ = m; }
    uint64_t Material() const { return material_; }
    void SetPipeline(uint64_t p) { pipeline_ = p; }
    uint64_t Pipeline() const { return pipeline_; }

    void AddItem(uint64_t instanceId, int64_t sortKey) {
        Item it; it.instanceId = instanceId; it.sortKey = sortKey;
        items_.push_back(it);
    }
    void Clear() { items_.clear(); }
    size_t Count() const { return items_.size(); }
    uint64_t ItemAt(size_t i) const {
        if (i >= items_.size()) return 0;
        return items_[i].instanceId;
    }
    bool IsEmpty() const { return items_.empty(); }

    // Sort items back-to-front by sort key (descending for transparent).
    void SortBackToFront() {
        for (size_t i = 0; i < items_.size(); ++i)
            for (size_t j = i + 1; j < items_.size(); ++j)
                if (items_[j].sortKey > items_[i].sortKey) {
                    Item t = items_[i]; items_[i] = items_[j]; items_[j] = t;
                }
    }

private:
    struct Item { uint64_t instanceId = 0; int64_t sortKey = 0; };
    uint64_t material_ = 0;
    uint64_t pipeline_ = 0;
    std::vector<Item> items_;
};

} // namespace bighero
