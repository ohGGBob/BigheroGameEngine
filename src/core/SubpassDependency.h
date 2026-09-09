#pragma once
#include <cstdint>
#include "ImageLayout_v2.h"

namespace bighero {

// SubpassDependency: describes a memory/execution dependency between two
// subpasses (or between an external pass and a subpass). Self-contained.
class SubpassDependency {
public:
    SubpassDependency() = default;
    SubpassDependency(uint32_t srcSubpass, uint32_t dstSubpass,
                      uint16_t srcStageMask, uint16_t dstStageMask,
                      uint16_t srcAccessMask = 0, uint16_t dstAccessMask = 0)
        : srcSubpass_(srcSubpass), dstSubpass_(dstSubpass),
          srcStageMask_(srcStageMask), dstStageMask_(dstStageMask),
          srcAccessMask_(srcAccessMask), dstAccessMask_(dstAccessMask) {}

    static const uint32_t External = 0xFFFFFFFFu;

    void SetSrcSubpass(uint32_t s) { srcSubpass_ = s; }
    uint32_t SrcSubpass() const { return srcSubpass_; }
    void SetDstSubpass(uint32_t s) { dstSubpass_ = s; }
    uint32_t DstSubpass() const { return dstSubpass_; }
    void SetSrcStageMask(uint16_t m) { srcStageMask_ = m; }
    uint16_t SrcStageMask() const { return srcStageMask_; }
    void SetDstStageMask(uint16_t m) { dstStageMask_ = m; }
    uint16_t DstStageMask() const { return dstStageMask_; }
    void SetSrcAccessMask(uint16_t m) { srcAccessMask_ = m; }
    uint16_t SrcAccessMask() const { return srcAccessMask_; }
    void SetDstAccessMask(uint16_t m) { dstAccessMask_ = m; }
    uint16_t DstAccessMask() const { return dstAccessMask_; }

    bool IsExternalSrc() const { return srcSubpass_ == External; }
    bool IsExternalDst() const { return dstSubpass_ == External; }
    bool IsInternal() const { return srcSubpass_ != External && dstSubpass_ != External; }

private:
    uint32_t srcSubpass_ = External, dstSubpass_ = External;
    uint16_t srcStageMask_ = 0, dstStageMask_ = 0;
    uint16_t srcAccessMask_ = 0, dstAccessMask_ = 0;
};

} // namespace bighero
