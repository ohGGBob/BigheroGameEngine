#pragma once
#include <cstdint>
#include <string>
#include <cstddef>

namespace bighero {

// Renderer component for a 2D sprite: texture/sprite handle, tint, flip and
// draw state. Pure data holder used by the 2D render systems.
class SpriteRenderer {
public:
    SpriteRenderer() {}
    SpriteRenderer(uint64_t texture, int spriteIndex, int material)
        : texture_(texture), sprite_(spriteIndex), material_(material) {}

    void SetTexture(uint64_t t) { texture_ = t; }
    uint64_t Texture() const { return texture_; }
    void SetSprite(int s) { sprite_ = s; }
    int Sprite() const { return sprite_; }
    void SetMaterial(int m) { material_ = m; }
    int Material() const { return material_; }

    // Tint color (RGBA packed as a 32-bit value).
    void SetColor(uint32_t c) { color_ = c; }
    uint32_t Color() const { return color_; }

    void SetFlipX(bool f) { flipX_ = f; }
    bool FlipX() const { return flipX_; }
    void SetFlipY(bool f) { flipY_ = f; }
    bool FlipY() const { return flipY_; }

    // Draw order / sorting layer (higher draws on top).
    void SetOrder(int o) { order_ = o; }
    int Order() const { return order_; }
    void SetLayer(const std::string& l) { layer_ = l; }
    const std::string& Layer() const { return layer_; }

    void SetVisible(bool v) { visible_ = v; }
    bool Visible() const { return visible_; }

    bool IsValid() const { return texture_ != 0; }

private:
    uint64_t texture_ = 0;
    int sprite_ = 0;
    int material_ = 0;
    uint32_t color_ = 0xFFFFFFFFu;
    bool flipX_ = false, flipY_ = false;
    int order_ = 0;
    std::string layer_;
    bool visible_ = true;
};

} // namespace bighero
