#pragma once
#include <cstdint>

namespace bighero {

// PipelineStage: bitmask of pipeline stages used for barriers and dependency
// tracking. Self-contained, std-lib only.
class PipelineStage {
public:
    static constexpr uint32_t None = 0;
    static constexpr uint32_t VertexInput = 1u << 0;
    static constexpr uint32_t VertexShader = 1u << 1;
    static constexpr uint32_t FragmentShader = 1u << 2;
    static constexpr uint32_t ComputeShader = 1u << 3;
    static constexpr uint32_t Transfer = 1u << 4;
    static constexpr uint32_t EarlyFragment = 1u << 5;
    static constexpr uint32_t LateFragment = 1u << 6;
    static constexpr uint32_t AllGraphics = VertexInput|VertexShader|FragmentShader|EarlyFragment|LateFragment;

    static bool IsGraphicsOnly(uint32_t s) { return (s & AllGraphics) != 0 && (s & ComputeShader) == 0; }
    static bool HasCompute(uint32_t s) { return (s & ComputeShader) != 0; }
    static bool HasTransfer(uint32_t s) { return (s & Transfer) != 0; }
    static uint32_t GraphicsStages() { return AllGraphics; }
};

} // namespace bighero
