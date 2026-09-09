#pragma once
#include <cstdint>

namespace bighero {

// ClearFlags: bitmask describing which buffers a render target clear touches
// (color, depth, stencil). Pure lightweight flags utility.
class ClearFlags {
public:
    enum Flag : std::uint32_t {
        None        = 0,
        Color       = 1u << 0,
        Depth       = 1u << 1,
        Stencil     = 1u << 2,
        All         = Color | Depth | Stencil
    };

    ClearFlags() : flags_(None) {}
    explicit ClearFlags(Flag f) : flags_(f) {}

    void Set(Flag f) { flags_ = static_cast<Flag>(flags_ | f); }
    void Clear(Flag f) { flags_ = static_cast<Flag>(flags_ & ~f); }
    bool Test(Flag f) const { return (flags_ & f) == f; }
    void SetAll() { flags_ = All; }
    void ClearAll() { flags_ = None; }

    ClearFlags operator|(ClearFlags o) const {
        return ClearFlags(static_cast<Flag>(flags_ | o.flags_));
    }
    ClearFlags operator&(ClearFlags o) const {
        return ClearFlags(static_cast<Flag>(flags_ & o.flags_));
    }
    bool operator==(ClearFlags o) const { return flags_ == o.flags_; }
    bool operator!=(ClearFlags o) const { return flags_ != o.flags_; }

    std::uint32_t Raw() const { return flags_; }
    static ClearFlags FromRaw(std::uint32_t r) { return ClearFlags(static_cast<Flag>(r)); }

    explicit operator bool() const { return flags_ != None; }

private:
    std::uint32_t flags_;
};

} // namespace bighero
