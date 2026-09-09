#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// FramebufferDescriptor: points a set of image views into a framebuffer with a
// given extent. Self-contained, std-lib only.
class FramebufferDescriptor {
public:
    FramebufferDescriptor() = default;
    FramebufferDescriptor(uint64_t renderPass, uint32_t width, uint32_t height)
        : renderPass_(renderPass), width_(width), height_(height) {}

    void SetRenderPass(uint64_t rp) { renderPass_ = rp; }
    uint64_t RenderPass() const { return renderPass_; }
    void SetExtent(uint32_t w, uint32_t h) { width_=w; height_=h; }
    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }

    void AddAttachment(uint64_t imageView, uint32_t layer = 0) {
        Attachment a; a.imageView = imageView; a.layer = layer;
        attachments_.push_back(a);
    }
    size_t AttachmentCount() const { return attachments_.size(); }
    const std::vector<uint64_t> AttachmentViews() const {
        std::vector<uint64_t> v;
        for (auto& a : attachments_) v.push_back(a.imageView);
        return v;
    }
    uint64_t AttachmentAt(size_t i) const { return attachments_[i].imageView; }
    void RemoveAll() { attachments_.clear(); }

    struct Attachment { uint64_t imageView = 0; uint32_t layer = 0; };
    bool IsValid() const {
        return renderPass_ != 0 && width_ > 0 && height_ > 0 && !attachments_.empty();
    }

private:
    uint64_t renderPass_ = 0;
    uint32_t width_ = 0, height_ = 0;
    std::vector<Attachment> attachments_;
};

} // namespace bighero
