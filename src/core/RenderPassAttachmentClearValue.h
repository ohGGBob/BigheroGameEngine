#pragma once
#include <cstdint>
#include "ClearColor.h"
#include "ClearDepthStencil.h"

namespace bighero {

// RenderPassAttachmentClearValue: the clear value (color or depth-stencil) for
// a render-pass attachment. Self-contained, std-lib only.
class RenderPassAttachmentClearValue {
public:
    enum class Kind : uint8_t { None = 0, Color = 1, DepthStencil = 2 };

    RenderPassAttachmentClearValue() = default;
    static RenderPassAttachmentClearValue Color(ClearColor color) {
        RenderPassAttachmentClearValue v; v.kind_ = Kind::Color; v.color_ = color;
        return v;
    }
    static RenderPassAttachmentClearValue DepthStencil(ClearDepthStencil ds) {
        RenderPassAttachmentClearValue v; v.kind_ = Kind::DepthStencil; v.depthStencil_ = ds;
        return v;
    }

    void SetColor(ClearColor color) { kind_ = Kind::Color; color_ = color; }
    ClearColor GetColor() const { return color_; }
    void SetDepthStencil(ClearDepthStencil ds) { kind_ = Kind::DepthStencil; depthStencil_ = ds; }
    ClearDepthStencil GetDepthStencil() const { return depthStencil_; }

    Kind GetKind() const { return kind_; }
    bool IsColor() const { return kind_ == Kind::Color; }
    bool IsDepthStencil() const { return kind_ == Kind::DepthStencil; }
    bool IsNone() const { return kind_ == Kind::None; }
    bool IsValid() const { return kind_ != Kind::None; }

    static const char* KindName(Kind k) {
        switch (k) {
            case Kind::None: return "None";
            case Kind::Color: return "Color";
            case Kind::DepthStencil: return "DepthStencil";
        }
        return "Unknown";
    }

private:
    Kind kind_ = Kind::None;
    ClearColor color_;
    ClearDepthStencil depthStencil_;
};

} // namespace bighero
