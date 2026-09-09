#pragma once
#include <cstdint>
#include <vector>
#include "AttachmentDescription.h"

namespace bighero {

// RenderPassBeginInfo: describes how a render pass begins (which attachments
// and clear values to use). Self-contained, std-lib only.
class RenderPassBeginInfo {
public:
    RenderPassBeginInfo() = default;
    RenderPassBeginInfo(uint64_t renderPass, uint64_t framebuffer)
        : renderPass_(renderPass), framebuffer_(framebuffer) {}

    void SetRenderPass(uint64_t rp) { renderPass_ = rp; }
    uint64_t RenderPass() const { return renderPass_; }
    void SetFramebuffer(uint64_t fb) { framebuffer_ = fb; }
    uint64_t Framebuffer() const { return framebuffer_; }

    void SetExtent(uint32_t w, uint32_t h) { width_ = w; height_ = h; }
    uint32_t Width() const { return width_; }
    uint32_t Height() const { return height_; }
    void SetClearColorCount(uint32_t c) { clearCount_ = c; }
    uint32_t ClearColorCount() const { return clearCount_; }

    void SetOffset(int32_t x, int32_t y) { offsetX_ = x; offsetY_ = y; }
    int32_t OffsetX() const { return offsetX_; }
    int32_t OffsetY() const { return offsetY_; }

    void AddClearRect(uint32_t index, bool isColor, bool isDepthStencil) {
        ClearRect c; c.index = index; c.isColor = isColor; c.isDepthStencil = isDepthStencil;
        clearRects_.push_back(c);
    }
    size_t ClearRectCount() const { return clearRects_.size(); }

    struct ClearRect {
        uint32_t index = 0;
        bool isColor = false;
        bool isDepthStencil = false;
    };
    const ClearRect& ClearRectAt(size_t i) const { return clearRects_[i]; }

    bool IsValid() const { return renderPass_ != 0 && framebuffer_ != 0 && width_ > 0 && height_ > 0; }

private:
    uint64_t renderPass_ = 0;
    uint64_t framebuffer_ = 0;
    uint32_t width_ = 0, height_ = 0;
    int32_t offsetX_ = 0, offsetY_ = 0;
    uint32_t clearCount_ = 0;
    std::vector<ClearRect> clearRects_;
};

} // namespace bighero
