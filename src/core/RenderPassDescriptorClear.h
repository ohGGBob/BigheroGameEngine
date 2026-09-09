#pragma once
#include <cstdint>
#include "RenderPassAttachmentClearValue_v2.h"

namespace bighero {

// RenderPassDescriptorClear: a helper that describes which attachments should
// be cleared and with what values when a render pass begins. Self-contained.
class RenderPassDescriptorClear {
public:
    RenderPassDescriptorClear() = default;

    void SetClearCount(uint32_t c) { clearCount_ = c; }
    uint32_t ClearCount() const { return clearCount_; }
    void SetAttachmentIndex(uint32_t i) { attachmentIndex_ = i; }
    uint32_t AttachmentIndex() const { return attachmentIndex_; }

    void AddColorClear(ClearColor color) {
        RenderPassAttachmentClearValue v = RenderPassAttachmentClearValue::Color(color);
        clears_.push_back(v);
    }
    void AddDepthClear(ClearDepthStencil ds) {
        RenderPassAttachmentClearValue v = RenderPassAttachmentClearValue::DepthStencil(ds);
        clears_.push_back(v);
    }
    size_t ClearValueCount() const { return clears_.size(); }
    const RenderPassAttachmentClearValue& ClearValueAt(size_t i) const { return clears_[i]; }

    void ClearAll() { clears_.clear(); }
    bool IsEmpty() const { return clears_.empty(); }

private:
    uint32_t clearCount_ = 0;
    uint32_t attachmentIndex_ = 0;
    std::vector<RenderPassAttachmentClearValue> clears_;
};

} // namespace bighero
