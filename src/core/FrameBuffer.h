#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace bighero {

// Framebuffer: logical collection of color/depth attachments plus a size.
// CPU-side handle; the real object is created by the graphics backend.
class FrameBuffer {
public:
    FrameBuffer() {}
    FrameBuffer(int width, int height) : w_(width), h_(height) {}

    void Resize(int w, int h) { w_ = w; h_ = h; }
    int Width() const { return w_; }
    int Height() const { return h_; }
    bool IsValid() const { return w_ > 0 && h_ > 0; }

    void AttachColor(std::uint64_t target, int slot = 0) {
        if (slot >= (int)colorTarget_.size()) colorTarget_.resize(slot + 1, 0);
        colorTarget_[slot] = target;
    }
    std::uint64_t ColorTarget(int slot = 0) const {
        return slot < (int)colorTarget_.size() ? colorTarget_[slot] : 0;
    }
    int ColorAttachmentCount() const { return (int)colorTarget_.size(); }

    void SetDepthTarget(std::uint64_t t) { depthTarget_ = t; }
    std::uint64_t DepthTarget() const { return depthTarget_; }

    void SetClearColor(float r, float g, float b, float a = 1.0f) {
        clear_[0]=r; clear_[1]=g; clear_[2]=b; clear_[3]=a;
    }
    void GetClearColor(float& r, float& g, float& b, float& a) const {
        r=clear_[0]; g=clear_[1]; b=clear_[2]; a=clear_[3];
    }

private:
    int w_ = 0, h_ = 0;
    std::vector<std::uint64_t> colorTarget_;
    std::uint64_t depthTarget_ = 0;
    float clear_[4] = {0,0,0,1};
};

} // namespace bighero
