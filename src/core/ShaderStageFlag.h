#pragma once
#include <cstdint>

namespace bighero {

// ShaderStageFlag: bitmask helpers for shader stage flags used across the
// render backend. Self-contained, std-lib only.
class ShaderStageFlag {
public:
    enum : uint16_t {
        Vertex   = 0x0001,
        Fragment = 0x0002,
        Compute  = 0x0004,
        Geometry = 0x0008,
        TessControl = 0x0010,
        TessEval    = 0x0020,
        AllGraphics = Vertex | Fragment | Geometry | TessControl | TessEval,
        All = AllGraphics | Compute
    };

    ShaderStageFlag() = default;
    explicit ShaderStageFlag(uint16_t flags) : flags_(flags) {}

    void SetFlags(uint16_t f) { flags_ = f; }
    uint16_t Flags() const { return flags_; }
    void Add(uint16_t f) { flags_ |= f; }
    void Remove(uint16_t f) { flags_ &= (uint16_t)~f; }
    bool Has(uint16_t f) const { return (flags_ & f) == f; }
    bool IsEmpty() const { return flags_ == 0; }
    void Clear() { flags_ = 0; }

    bool HasVertex() const { return Has(Vertex); }
    bool HasFragment() const { return Has(Fragment); }
    bool HasCompute() const { return Has(Compute); }
    bool IsGraphicsOnly() const { return (flags_ & AllGraphics) != 0 && !Has(Compute); }
    bool IsComputeOnly() const { return Has(Compute) && (flags_ & AllGraphics) == 0; }

    static const char* ToString(uint16_t f) {
        if (f == Vertex) return "Vertex";
        if (f == Fragment) return "Fragment";
        if (f == Compute) return "Compute";
        if (f == Geometry) return "Geometry";
        if (f == AllGraphics) return "AllGraphics";
        if (f == All) return "All";
        return "Mixed";
    }

private:
    uint16_t flags_ = 0;
};

} // namespace bighero
