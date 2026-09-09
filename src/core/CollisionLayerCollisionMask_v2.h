#pragma once
#include <cstdint>

namespace bighero {

// CollisionLayerCollisionMask: manages collision layer bitmasks telling which
// layers can collide with which. Self-contained, std-lib only.
class CollisionLayerCollisionMask {
public:
    CollisionLayerCollisionMask() = default;

    // Set whether layer A may collide with layer B (symmetric).
    void SetCollision(int layerA, int layerB, bool canCollide) {
        if (layerA < 0 || layerA >= kMaxLayers) return;
        if (layerB < 0 || layerB >= kMaxLayers) return;
        if (canCollide) { mask_[layerA] |= (1u << layerB); mask_[layerB] |= (1u << layerA); }
        else { mask_[layerA] &= ~(1u << layerB); mask_[layerB] &= ~(1u << layerA); }
    }

    bool CanCollide(int layerA, int layerB) const {
        if (layerA < 0 || layerA >= kMaxLayers) return false;
        if (layerB < 0 || layerB >= kMaxLayers) return false;
        return (mask_[layerA] & (1u << layerB)) != 0;
    }

    // Get the collision mask for a layer (which layers it can hit).
    uint32_t MaskFor(int layer) const {
        if (layer < 0 || layer >= kMaxLayers) return 0;
        return mask_[layer];
    }

    // The layer of an object given its own mask (for query filtering).
    static int GetLayer(uint32_t layerMask) {
        for (int i = 0; i < kMaxLayers; ++i) if (layerMask & (1u << i)) return i;
        return 0;
    }

    void Reset() { for (int i=0;i<kMaxLayers;++i) mask_[i]=0; }
    // Enable collision of every layer with every other layer (default).
    void EnableAll() {
        for (int i=0;i<kMaxLayers;++i) mask_[i] = 0xFFFFFFFFu;
    }
    static constexpr int kMaxLayers = 32;

private:
    uint32_t mask_[kMaxLayers] = {0};
};

} // namespace bighero
