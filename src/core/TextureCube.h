#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// TextureCube: a cube map texture made of six 2D faces (positive/negative
// X/Y/Z). Stores per-face pixel data as float[4] (RGBA). Self-contained.
class TextureCube {
public:
    enum Face : int { PosX = 0, NegX = 1, PosY = 2, NegY = 3, PosZ = 4, NegZ = 5, FaceCount = 6 };

    TextureCube() = default;
    TextureCube(uint32_t size, uint32_t channels = 4)
        : size_(size), channels_(channels) {
        Allocate(size, channels);
    }

    void Allocate(uint32_t size, uint32_t channels = 4) {
        size_ = size;
        channels_ = channels;
        size_t count = (size_t)size * size * channels;
        faces_[0].assign(count, 0.0f);
        faces_[1].assign(count, 0.0f);
        faces_[2].assign(count, 0.0f);
        faces_[3].assign(count, 0.0f);
        faces_[4].assign(count, 0.0f);
        faces_[5].assign(count, 0.0f);
    }

    uint32_t Size() const { return size_; }
    uint32_t Channels() const { return channels_; }
    bool Empty() const { return faces_[0].empty(); }

    std::vector<float>& Face(int f) { return faces_[f & 5]; }
    const std::vector<float>& Face(int f) const { return faces_[f & 5]; }

    bool IsSquare() const { return !Empty() && size_ > 0; }

private:
    std::vector<float> faces_[6];
    uint32_t size_ = 0;
    uint32_t channels_ = 4;
};

} // namespace bighero
