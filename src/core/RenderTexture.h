#pragma once
#include <cstdint>

namespace bighero {

// RenderTexture: a CPU-side render target descriptor (size, format, flags),
// which the renderer maps to a GPU texture. Self-contained descriptor.
struct RenderTexture {
    // Texture format enum (subset of common render formats).
    enum class Format {
        RGBA8, RGBA16F, RGBA32F, R8, R16F, R32F,
        Depth16, Depth24Stencil8, Default
    };

    int width = 1, height = 1;
    Format format = Format::RGBA8;
    bool useMipmaps = false;
    bool depthBuffer = true;
    bool screenSpace = false;   // true binds to the backbuffer dimensions
    uint32_t flags = 0;

    enum Flag : uint32_t {
        Flag_RenderTarget = 1u << 0,
        Flag_ShaderResource = 1u << 1,
        Flag_DepthOnly = 1u << 2,
        Flag_ClampToEdge = 1u << 3
    };

    RenderTexture() = default;
    RenderTexture(int w, int h, Format fmt = Format::RGBA8)
        : width(w), height(h), format(fmt) {}

    void Resize(int w, int h) { width = w < 1 ? 1 : w; height = h < 1 ? 1 : h; }
    bool HasFlag(uint32_t f) const { return (flags & f) != 0; }
    void SetFlag(uint32_t f) { flags |= f; }
    float Aspect() const { return height > 0 ? (float)width / height : 1.0f; }
};

} // namespace bighero
