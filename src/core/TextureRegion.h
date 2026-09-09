#pragma once
#include <cstdint>

namespace bighero {

// TextureRegion: a rectangular sub-region of a texture (in UV space),
// optionally flipped. Used to reference a frame inside an atlas.
class TextureRegion {
public:
    TextureRegion() {}
    TextureRegion(float u0, float v0, float u1, float v1)
        : u0_(u0), v0_(v0), u1_(u1), v1_(v1) {}

    // Normalize/validate UVs to [0,1].
    void SetUV(float u0, float v0, float u1, float v1) {
        u0_ = Clamp01(u0); v0_ = Clamp01(v0);
        u1_ = Clamp01(u1); v1_ = Clamp01(v1);
    }
    void UV(float& u0, float& v0, float& u1, float& v1) const {
        u0 = u0_; v0 = v0_; u1 = u1_; v1 = v1_;
    }

    // Pixel-space region on a texture of (tw, th).
    void SetPixelRect(int x, int y, int w, int h, int tw, int th) {
        if (tw <= 0 || th <= 0) return;
        u0_ = (float)x / tw;      v0_ = (float)y / th;
        u1_ = (float)(x + w) / tw; v1_ = (float)(y + h) / th;
    }

    void SetFlip(bool fx, bool fy) { flipX_ = fx; flipY_ = fy; }
    bool FlipX() const { return flipX_; }
    bool FlipY() const { return flipY_; }
    void SetTexture(std::uint64_t tex) { tex_ = tex; }
    std::uint64_t Texture() const { return tex_; }

    float Width() const { return u1_ - u0_; }
    float Height() const { return v1_ - v0_; }
    bool IsEmpty() const { return Width() <= 0 || Height() <= 0; }

    // Rotate (90-degree steps) the UV region by swapping the U and V axes.
    void Rotate90() {
        float nu0 = v0_, nv0 = u0_;
        float nu1 = v1_, nv1 = u1_;
        u0_ = nu0; v0_ = nv0;
        u1_ = nu1; v1_ = nv1;
        // Keep min/max ordering valid after the swap.
        if (u0_ > u1_) { float t = u0_; u0_ = u1_; u1_ = t; }
        if (v0_ > v1_) { float t = v0_; v0_ = v1_; v1_ = t; }
    }
    void Reset() { u0_ = v0_ = 0; u1_ = v1_ = 1; }

private:
    static float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    float u0_ = 0, v0_ = 0, u1_ = 1, v1_ = 1;
    bool flipX_ = false, flipY_ = false;
    std::uint64_t tex_ = 0;
};

} // namespace bighero
