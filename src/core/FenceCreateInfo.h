#pragma once
#include <cstdint>

namespace bighero {

// FenceCreateInfo: describes how to create a fence (initially signaled or
// not). Self-contained, std-lib only.
class FenceCreateInfo {
public:
    enum Flag : uint32_t { None = 0, Signaled = 1 };

    FenceCreateInfo() = default;
    explicit FenceCreateInfo(uint32_t flags) : flags_(flags) {}

    void SetFlags(uint32_t f) { flags_ = f; }
    uint32_t Flags() const { return flags_; }
    void AddFlag(Flag f) { flags_ |= static_cast<uint32_t>(f); }
    bool HasFlag(Flag f) const { return (flags_ & static_cast<uint32_t>(f)) != 0; }

    void SetInitiallySignaled(bool b) {
        if (b) flags_ |= static_cast<uint32_t>(Flag::Signaled);
        else flags_ &= ~static_cast<uint32_t>(Flag::Signaled);
    }
    bool IsInitiallySignaled() const { return HasFlag(Flag::Signaled); }

    void SetName(const char* name) { name_ = name ? name : ""; }
    const char* Name() const { return name_; }

private:
    uint32_t flags_ = 0;
    const char* name_ = "";
};

} // namespace bighero
