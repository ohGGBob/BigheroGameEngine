#pragma once
#include <cstdint>

namespace bighero {

// TessellationState: describes tessellation control/output state for a
// tessellation shader stage (control points, patch type). Self-contained.
class TessellationState {
public:
    enum class PatchType : uint8_t { Triangle = 0, Quad = 1, Isoline = 2 };

    TessellationState() = default;

    void SetControlPoints(uint32_t c) { controlPoints_ = c; }
    uint32_t ControlPoints() const { return controlPoints_; }

    void SetPatchType(PatchType t) { patchType_ = t; }
    PatchType GetPatchType() const { return patchType_; }

    void SetOutputTriangles(bool b) { outputTriangles_ = b; hasOutput_ = true; }
    void SetOutputQuads(bool b) { outputQuads_ = b; hasOutput_ = true; }
    void SetOutputIsolines(bool b) { outputIsolines_ = b; hasOutput_ = true; }
    bool HasOutput() const { return hasOutput_; }

    void SetSpacingPowerOfTwo(bool b) { spacingPow2_ = b; }
    bool SpacingPowerOfTwo() const { return spacingPow2_; }

    void SetMaxTessellationFactor(float f) { maxFactor_ = f; }
    float MaxTessellationFactor() const { return maxFactor_; }

    bool IsValid() const { return controlPoints_ > 0 && controlPoints_ <= 32; }
    static const char* PatchTypeName(PatchType t) {
        switch (t) {
            case PatchType::Triangle: return "Triangle";
            case PatchType::Quad: return "Quad";
            case PatchType::Isoline: return "Isoline";
        }
        return "Unknown";
    }

private:
    uint32_t controlPoints_ = 3;
    PatchType patchType_ = PatchType::Triangle;
    bool outputTriangles_ = false, outputQuads_ = false, outputIsolines_ = false;
    bool hasOutput_ = false;
    bool spacingPow2_ = false;
    float maxFactor_ = 64.0f;
};

} // namespace bighero
