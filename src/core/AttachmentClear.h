#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// AttachmentClear: describes the clear value for a render-pass attachment.
// Self-contained, std-lib only.
class AttachmentClear {
public:
    AttachmentClear() = default;
    AttachmentClear(uint32_t attachment, float r, float g, float b, float a = 1.0f)
        : attachment_(attachment), r_(r), g_(g), b_(b), a_(a) {}

    void SetAttachment(uint32_t att) { attachment_ = att; }
    uint32_t Attachment() const { return attachment_; }
    void SetColor(float r, float g, float b, float a = 1.0f) { r_=r; g_=g; b_=b; a_=a; }
    void SetDepthStencil(float depth, uint32_t stencil) { depth_=depth; stencil_=stencil; hasDepth_=true; }
    void SetDepth(float d) { depth_=d; }
    float Depth() const { return depth_; }
    void SetStencil(uint32_t s) { stencil_=s; }
    uint32_t Stencil() const { return stencil_; }

    float R() const { return r_; }
    float G() const { return g_; }
    float B() const { return b_; }
    float A() const { return a_; }
    bool HasDepthClear() const { return hasDepth_; }

private:
    uint32_t attachment_ = 0;
    float r_=0, g_=0, b_=0, a_=1, depth_=1;
    uint32_t stencil_ = 0;
    bool hasDepth_ = false;
};

} // namespace bighero
