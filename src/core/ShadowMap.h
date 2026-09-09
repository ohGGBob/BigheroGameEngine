#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace bighero {

// ShadowMap: manages a shadow map pass — light-space viewport, resolution,
// enabled cascades, and bias. CPU-side config for the GPU shadow pass.
class ShadowMap {
public:
    ShadowMap() {}
    explicit ShadowMap(int size) { Resize(size); }

    void Resize(int size) { size_ = size < 1 ? 1 : size; }
    int Size() const { return size_; }

    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const { return enabled_; }
    void SetCascadeCount(int c) { cascades_ = c < 1 ? 1 : (c > 4 ? 4 : c); }
    int CascadeCount() const { return cascades_; }
    void SetBias(float b) { bias_ = b; }
    float Bias() const { return bias_; }

    // Light-space viewport (orthographic bounds).
    void SetViewport(float w, float h, float nearZ, float farZ) {
        vw_ = w < 0 ? 0 : w; vh_ = h < 0 ? 0 : h; nz_ = nearZ; fz_ = farZ;
    }
    void Viewport(float& w, float& h, float& nz, float& fz) const { w=vw_; h=vh_; nz=nz_; fz=fz_; }

    void SetTexture(std::uint64_t id) { texId_ = id; }
    std::uint64_t Texture() const { return texId_; }

    // Framebuffer allocation hint (bytes).
    std::uint64_t AllocatedBytes() const {
        float depthBytes = (float)size_*(float)size_*4.0f;
        return (std::uint64_t)(depthBytes * (float)cascades_);
    }

    void Reset() { enabled_ = false; size_ = 1024; cascades_ = 1; bias_ = 0.001f; }

private:
    bool enabled_ = false;
    int size_ = 1024;
    int cascades_ = 1;
    float bias_ = 0.001f;
    float vw_ = 0, vh_ = 0, nz_ = 0, fz_ = 0;
    std::uint64_t texId_ = 0;
};

} // namespace bighero
