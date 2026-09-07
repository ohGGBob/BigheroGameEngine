#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace bighero {

// A sprite sheet: an atlas of named frames, each a sub-rectangle of a texture.
class SpriteSheet {
public:
    struct Frame {
        std::string name;
        int x, y, w, h;          // sub-rectangle in texture pixels
    };

    // Add a frame, returning its index (or -1 on zero-size).
    int AddFrame(const char* name, int x, int y, int w, int h) {
        if (w <= 0 || h <= 0) return -1;
        frames_.push_back({std::string(name), x, y, w, h});
        return (int)frames_.size() - 1;
    }

    // Find a frame by name; returns -1 if not found.
    int FindFrame(const char* name) const {
        for (std::size_t i = 0; i < frames_.size(); ++i)
            if (frames_[i].name == name) return (int)i;
        return -1;
    }

    const Frame* GetFrame(int index) const {
        if (index < 0 || index >= (int)frames_.size()) return nullptr;
        return &frames_[index];
    }

    // Normalized UV coords [0,1] of a frame, given texture dimensions.
    void GetUVs(int index, float texW, float texH,
                float& u0, float& v0, float& u1, float& v1) const {
        const Frame* f = GetFrame(index);
        if (!f || texW <= 0 || texH <= 0) { u0=v0=0; u1=v1=1; return; }
        u0 = (float)f->x / texW;
        v0 = (float)f->y / texH;
        u1 = (float)(f->x + f->w) / texW;
        v1 = (float)(f->y + f->h) / texH;
    }

    std::size_t FrameCount() const { return frames_.size(); }
    bool Empty() const { return frames_.empty(); }
    void Clear() { frames_.clear(); }

    void SetTextureSize(int w, int h) { texW_ = w; texH_ = h; }

private:
    std::vector<Frame> frames_;
    int texW_ = 0, texH_ = 0;
};

} // namespace bighero
