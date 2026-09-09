#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// Renderer: a drawable component descriptor storing which mesh/material pair
// it renders plus render-order, layer, and visibility flags the culling pass
// uses. Self-contained, std-lib only.
class Renderer {
public:
    enum class Priority { Background, Geometry, Overlay, UI };

    Renderer() = default;

    void SetMaterial(uint64_t materialId) { materialId_ = materialId; }
    uint64_t GetMaterial() const { return materialId_; }
    void SetMesh(uint64_t meshId) { meshId_ = meshId; }
    uint64_t GetMesh() const { return meshId_; }

    // Rendering order within a layer (lower draws first; larger on top).
    void SetOrder(int order) { order_ = order; }
    int GetOrder() const { return order_; }
    void SetPriority(Priority p) { priority_ = p; }
    Priority GetPriority() const { return priority_; }

    void SetLayer(int layer) { layer_ = layer; }
    int GetLayer() const { return layer_; }

    bool IsVisible() const { return visible_; }
    void SetVisible(bool v) { visible_ = v; }
    // Culling-group mask for camera layer culling.
    void SetCullingMask(uint32_t mask) { cullingMask_ = mask; }
    uint32_t GetCullingMask() const { return cullingMask_; }

    // Whether this renderer is affected by per-object bounds frustum culling.
    void SetFrustumCulled(bool v) { frustumCulled_ = v; }
    bool IsFrustumCulled() const { return frustumCulled_; }

private:
    uint64_t materialId_ = 0;
    uint64_t meshId_ = 0;
    int order_ = 0;
    Priority priority_ = Priority::Geometry;
    int layer_ = 0;
    uint32_t cullingMask_ = 0xFFFFFFFFu;
    bool visible_ = true;
    bool frustumCulled_ = true;
};

} // namespace bighero
