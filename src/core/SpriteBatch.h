#pragma once
#include <vector>
#include <cstddef>
#include <cstdint>

namespace bighero {

// SpriteBatch: collect draw calls for batches of 2D sprites (quad + UV +
// color + texture id) so the backend can sort/flush in one go. Pure
// command-list container.
class SpriteBatch {
public:
    struct Sprite {
        float x, y;         // center
        float w, h;         // size
        float u0, v0, u1, v1;
        std::uint64_t texture;
        float r, g, b, a;
        int order;          // draw order
    };

    SpriteBatch() {}

    void Clear() { sprites_.clear(); }

    void Add(float x, float y, float w, float h, std::uint64_t tex,
             float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1,
             float r = 1, float g = 1, float b = 1, float a = 1, int order = 0) {
        Sprite s;
        s.x = x; s.y = y; s.w = w; s.h = h;
        s.u0 = u0; s.v0 = v0; s.u1 = u1; s.v1 = v1;
        s.texture = tex;
        s.r = r; s.g = g; s.b = b; s.a = a;
        s.order = order;
        sprites_.push_back(s);
    }

    std::size_t Count() const { return sprites_.size(); }
    const Sprite& At(std::size_t i) const { return sprites_[i]; }
    bool Empty() const { return sprites_.empty(); }

    // Stable sort by texture then order (ascending). Groups sprites by
    // texture id so the backend minimizes state switches. `after(a,b)` is
    // true when a should be ordered after b.
    void SortByTexture() {
        // insertion sort (stable, small N typical)
        for (std::size_t i = 1; i < sprites_.size(); ++i) {
            Sprite key = sprites_[i];
            std::size_t j = i;
            while (j > 0 && After(sprites_[j - 1], key)) {
                sprites_[j] = sprites_[j - 1];
                --j;
            }
            sprites_[j] = key;
        }
    }

private:
    static bool After(const Sprite& a, const Sprite& b) {
        if (a.texture != b.texture) return a.texture > b.texture;
        return a.order > b.order;
    }

    std::vector<Sprite> sprites_;
};

} // namespace bighero
