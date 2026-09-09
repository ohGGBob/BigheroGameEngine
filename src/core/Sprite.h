#pragma once
#include <cstdint>

namespace bighero {

// Sprite: a flat 2D sprite descriptor (texture region + pivot + pixels-per-unit)
// used by sprite renderers and the 2D batching system. Self-contained.
class Sprite {
public:
    Sprite() = default;
    Sprite(uint64_t texture, float x, float y, float w, float h,
           float pivotX = 0.5f, float pivotY = 0.5f, float ppu = 100.0f)
        : texture_(texture), x_(x), y_(y), w_(w), h_(h),
          pivotX_(pivotX), pivotY_(pivotY), pixelsPerUnit_(ppu) {}

    void SetTexture(uint64_t tex) { texture_ = tex; }
    uint64_t GetTexture() const { return texture_; }

    void SetRect(float x, float y, float w, float h) {
        x_ = x; y_ = y; w_ = w; h_ = h;
    }
    void GetRect(float& x, float& y, float& w, float& h) const {
        x = x_; y = y_; w = w_; h = h_;
    }
    void SetPivot(float px, float py) { pivotX_ = px; pivotY_ = py; }
    void GetPivot(float& px, float& py) const { px = pivotX_; py = pivotY_; }

    float Width() const { return w_; }
    float Height() const { return h_; }
    // World-space size given the current pixels-per-unit scale.
    float WorldWidth() const { return pixelsPerUnit_ > 0 ? w_ / pixelsPerUnit_ : 0; }
    float WorldHeight() const { return pixelsPerUnit_ > 0 ? h_ / pixelsPerUnit_ : 0; }

private:
    uint64_t texture_ = 0;
    float x_ = 0, y_ = 0, w_ = 1, h_ = 1;
    float pivotX_ = 0.5f, pivotY_ = 0.5f;
    float pixelsPerUnit_ = 100.0f;
};

} // namespace bighero
