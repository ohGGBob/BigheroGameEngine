#pragma once
#include <cstdint>

namespace bighero {

// CommandBufferBeginInfo: describes how a command buffer begins recording
// (one-time submit flag, inheritance info). Self-contained, std-lib only.
class CommandBufferBeginInfo {
public:
    enum Flag : uint32_t {
        OneTimeSubmit = 1,
        RenderPassContinue = 2,
        SimultaneousUse = 4
    };

    CommandBufferBeginInfo() = default;

    void SetFlags(uint32_t f) { flags_ = f; }
    uint32_t Flags() const { return flags_; }
    void AddFlag(Flag f) { flags_ |= static_cast<uint32_t>(f); }
    void RemoveFlag(Flag f) { flags_ &= ~static_cast<uint32_t>(f); }
    bool HasFlag(Flag f) const { return (flags_ & static_cast<uint32_t>(f)) != 0; }

    void SetInheritance(uint32_t renderPass = 0, uint32_t subpass = 0, uint64_t framebuffer = 0) {
        inherit_.renderPass = renderPass; inherit_.subpass = subpass; inherit_.framebuffer = framebuffer;
        hasInheritance_ = true;
    }
    bool HasInheritance() const { return hasInheritance_; }
    uint64_t InheritFramebuffer() const { return inherit_.framebuffer; }

    bool IsOneTimeSubmit() const { return HasFlag(OneTimeSubmit); }
    bool IsSimultaneousUse() const { return HasFlag(SimultaneousUse); }

    static const char* FlagName(uint32_t f) {
        switch (f) {
            case OneTimeSubmit: return "OneTimeSubmit";
            case RenderPassContinue: return "RenderPassContinue";
            case SimultaneousUse: return "SimultaneousUse";
            default: return "Mixed";
        }
    }

private:
    struct Inherit { uint32_t renderPass = 0; uint32_t subpass = 0; uint64_t framebuffer = 0; };
    uint32_t flags_ = 0;
    Inherit inherit_;
    bool hasInheritance_ = false;
};

} // namespace bighero
