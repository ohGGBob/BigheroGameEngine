#pragma once
#include <cstdint>

namespace bighero {

// SortingLayer: a 2D render ordering layer descriptor (sorting layer ID +
// order in layer) used by the sprite/UI batching to establish draw order.
struct SortingLayer {
    uint32_t id = 0;          // sorting layer identifier
    int orderInLayer = 0;     // relative order within the layer
    int layerIndex = 0;       // index into the sorted layer list (0 = first)
    bool visible = true;

    SortingLayer() = default;
    SortingLayer(uint32_t id_, int orderInLayer_, int layerIndex_ = 0)
        : id(id_), orderInLayer(orderInLayer_), layerIndex(layerIndex_) {}

    // Sort key: higher layers first, then higher order-in-layer first.
    int64_t SortKey() const {
        // Encode layer index as high bits, order as low bits.
        return ((int64_t)layerIndex << 32) | (uint32_t)(orderInLayer + 0x80000000);
    }
    // Whether this layer should render over another.
    bool IsAbove(const SortingLayer& o) const { return SortKey() > o.SortKey(); }
    void SetVisible(bool v) { visible = v; }
};

} // namespace bighero
