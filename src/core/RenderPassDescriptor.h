#pragma once
#include <cstdint>
#include <vector>
#include "AttachmentDescription.h"
#include "SubpassDescription.h"
#include "SubpassDependency.h"

namespace bighero {

// RenderPassDescriptor: a complete description of a render pass, holding its
// attachments, subpasses, and dependencies. Self-contained, std-lib only.
class RenderPassDescriptor {
public:
    RenderPassDescriptor() = default;
    explicit RenderPassDescriptor(uint64_t renderPass) : renderPass_(renderPass) {}

    void SetRenderPass(uint64_t rp) { renderPass_ = rp; }
    uint64_t RenderPass() const { return renderPass_; }

    void AddAttachment(const AttachmentDescription& a) { attachments_.push_back(a); }
    size_t AttachmentCount() const { return attachments_.size(); }
    const AttachmentDescription& AttachmentAt(size_t i) const { return attachments_[i]; }
    void RemoveAttachments() { attachments_.clear(); }

    void AddSubpass(const SubpassDescription& s) { subpasses_.push_back(s); }
    size_t SubpassCount() const { return subpasses_.size(); }
    const SubpassDescription& SubpassAt(size_t i) const { return subpasses_[i]; }

    void AddDependency(const SubpassDependency& d) { dependencies_.push_back(d); }
    size_t DependencyCount() const { return dependencies_.size(); }

    bool IsValid() const { return renderPass_ != 0 && !attachments_.empty() && !subpasses_.empty(); }
    uint32_t TotalAttachments() const { return (uint32_t)attachments_.size(); }

private:
    uint64_t renderPass_ = 0;
    std::vector<AttachmentDescription> attachments_;
    std::vector<SubpassDescription> subpasses_;
    std::vector<SubpassDependency> dependencies_;
};

} // namespace bighero
