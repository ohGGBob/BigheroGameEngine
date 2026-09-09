#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace bighero {

// RendererData: per-object renderer configuration — visible layer, sorting
// order, material, cast-shadow flag, and a custom render-queue override.
// Pure descriptor consumed by the render pass / culling.
class RendererData {
public:
    RendererData() {}
    explicit RendererData(std::uint64_t materialId) : materialId_(materialId) {}

    void SetMaterialId(std::uint64_t id) { materialId_ = id; }
    std::uint64_t MaterialId() const { return materialId_; }
    void SetLayer(int l) { layer_ = l; }
    int Layer() const { return layer_; }
    void SetSortingOrder(int o) { sortingOrder_ = o; }
    int SortingOrder() const { return sortingOrder_; }
    void SetRenderQueue(int q) { queue_ = q; }
    int RenderQueue() const { return queue_; }
    void SetCastShadows(bool c) { castShadows_ = c; }
    bool CastShadows() const { return castShadows_; }
    void SetReceiveShadows(bool r) { receiveShadows_ = r; }
    bool ReceiveShadows() const { return receiveShadows_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetName(const char* n) { name_ = n ? n : ""; }
    const char* Name() const { return name_.c_str(); }

private:
    std::uint64_t materialId_ = 0;
    int layer_ = 0, sortingOrder_ = 0, queue_ = 2000;
    bool castShadows_ = true, receiveShadows_ = true, enabled_ = true;
    std::string name_;
};

} // namespace bighero
