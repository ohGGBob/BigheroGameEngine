#pragma once
#include <cstdint>

namespace bighero {

// AttachmentReference: reference to an attachment in a subpass (index +
// layout). Self-contained, std-lib only.
class AttachmentReference {
public:
    AttachmentReference() = default;
    AttachmentReference(uint32_t attachment, uint32_t layout)
        : attachment_(attachment), layout_(layout) {}

    void SetAttachment(uint32_t a) { attachment_ = a; }
    uint32_t Attachment() const { return attachment_; }
    void SetLayout(uint32_t l) { layout_ = l; }
    uint32_t Layout() const { return layout_; }
    void Set(uint32_t a, uint32_t l) { attachment_=a; layout_=l; }

    bool IsValid() const { return attachment_ != 0xFFFFFFFFu; }
    bool UsesLayout(uint32_t l) const { return layout_ == l; }
    static constexpr uint32_t UnusedAttachment() { return 0xFFFFFFFFu; }

private:
    uint32_t attachment_ = 0xFFFFFFFFu, layout_ = 0;
};

} // namespace bighero
