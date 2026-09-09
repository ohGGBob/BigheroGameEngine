#pragma once
#include <cstdint>

namespace bighero {

// BlendFactor: blend source/destination factors for color blending.
// Self-contained, std-lib only.
class BlendFactor {
public:
    enum class Factor : uint8_t { Zero=0, One=1, SrcColor=2, OneMinusSrcColor=3, DstColor=4, OneMinusDstColor=5, SrcAlpha=6, OneMinusSrcAlpha=7, DstAlpha=8, OneMinusDstAlpha=9 };

    static bool IsValid(Factor f) { return f<=Factor::OneMinusDstAlpha; }
    static bool IsAlphaRelated(Factor f) { return f==Factor::SrcAlpha || f==Factor::OneMinusSrcAlpha || f==Factor::DstAlpha || f==Factor::OneMinusDstAlpha; }
    static bool IsColorRelated(Factor f) { return f==Factor::SrcColor || f==Factor::OneMinusSrcColor || f==Factor::DstColor || f==Factor::OneMinusDstColor; }
    static const char* Name(Factor f) {
        switch (f) {
            case Factor::Zero: return "Zero"; case Factor::One: return "One";
            case Factor::SrcColor: return "SrcColor"; case Factor::OneMinusSrcColor: return "OneMinusSrcColor";
            case Factor::DstColor: return "DstColor"; case Factor::OneMinusDstColor: return "OneMinusDstColor";
            case Factor::SrcAlpha: return "SrcAlpha"; case Factor::OneMinusSrcAlpha: return "OneMinusSrcAlpha";
            case Factor::DstAlpha: return "DstAlpha"; case Factor::OneMinusDstAlpha: return "OneMinusDstAlpha";
        }
        return "Unknown";
    }
};

} // namespace bighero
