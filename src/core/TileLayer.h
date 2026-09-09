#pragma once
#include <cstdint>

namespace bighero {

// TileLayer: identifies a tiled rendering layer (index + kind + flags) used
// to organize tilemap layers, blending, and culling. Self-contained.
struct TileLayer {
    int32_t index = 0;
    uint32_t flags = 0;
    bool visible = true;

    enum Flag : uint32_t {
        Flag_WorldSpace  = 1u << 0,
        Flag_ScreenSpace = 1u << 1,
        Flag_Blend       = 1u << 2,
        Flag_Cull        = 1u << 3
    };

    TileLayer() = default;
    explicit TileLayer(int32_t index_, uint32_t flags_ = 0, bool visible_ = true)
        : index(index_), flags(flags_), visible(visible_) {}

    bool HasFlag(uint32_t f) const { return (flags & f) != 0; }
    void SetFlag(uint32_t f) { flags |= f; }
    void ClearFlag(uint32_t f) { flags &= ~f; }
    bool IsWorldSpace() const { return HasFlag(Flag_WorldSpace); }
};

} // namespace bighero
