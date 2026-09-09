#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// SubpassDescription: describes a subpass within a render pass: its color,
// depth-stencil, and input attachments. Self-contained, std-lib only.
class SubpassDescription {
public:
    SubpassDescription() = default;
    explicit SubpassDescription(uint32_t index) : index_(index) {}

    void SetIndex(uint32_t i) { index_ = i; }
    uint32_t Index() const { return index_; }

    void AddColorAttachment(uint32_t attachment) { colorAttachments_.push_back(attachment); }
    void AddResolveAttachment(uint32_t attachment) { resolveAttachments_.push_back(attachment); }
    void SetDepthStencilAttachment(uint32_t attachment) { hasDepth_ = true; depthAttachment_ = attachment; }
    void ClearDepthStencilAttachment() { hasDepth_ = false; depthAttachment_ = 0; }
    void AddInputAttachment(uint32_t attachment) { inputAttachments_.push_back(attachment); }

    size_t ColorAttachmentCount() const { return colorAttachments_.size(); }
    const std::vector<uint32_t>& ColorAttachments() const { return colorAttachments_; }
    uint32_t ColorAttachmentAt(size_t i) const { return colorAttachments_[i]; }
    size_t ResolveAttachmentCount() const { return resolveAttachments_.size(); }
    size_t InputAttachmentCount() const { return inputAttachments_.size(); }
    bool HasDepthStencil() const { return hasDepth_; }
    uint32_t DepthStencilAttachment() const { return depthAttachment_; }

    uint32_t TotalColorBindings() const {
        return (uint32_t)(colorAttachments_.size() + inputAttachments_.size());
    }
    bool IsValid() const {
        return colorAttachments_.empty() == false || hasDepth_;
    }

private:
    uint32_t index_ = 0;
    std::vector<uint32_t> colorAttachments_;
    std::vector<uint32_t> resolveAttachments_;
    std::vector<uint32_t> inputAttachments_;
    bool hasDepth_ = false;
    uint32_t depthAttachment_ = 0;
};

} // namespace bighero
