#pragma once
#include <cstdint>

namespace bighero {

// PipelineState: a compact CPU-side graphics pipeline descriptor capturing
// the shader program id and all fixed-function states that the renderer
// binds when issuing a draw call. Self-contained, std-lib only.
struct PipelineState {
    // Shader program id (0 = none/fallback).
    uint64_t shader = 0;
    // Blend / depth / rasterizer state ids (0 = engine defaults).
    uint64_t blendState = 0;
    uint64_t depthState = 0;
    uint64_t rasterizerState = 0;
    // Primitive topology (0 = triangles, 1 = lines, 2 = points, 3 = strip, 4 = fan).
    uint32_t topology = 0;
    uint32_t vertexArray = 0;   // VAO binding id
    // Vertex format id used for shader input matching (0 = auto).
    uint32_t vertexFormat = 0;
    // Flags controlling the pipeline.
    uint32_t flags = 0;

    enum Flag : uint32_t {
        Flag_Wireframe = 1u << 0,
        Flag_DepthTest = 1u << 1,
        Flag_DepthWrite = 1u << 2,
        Flag_CullBack = 1u << 3,
        Flag_BlendEnable = 1u << 4,
        Flag_Multisample = 1u << 5
    };

    PipelineState() = default;

    void SetShader(uint64_t s) { shader = s; }
    void SetTopology(uint32_t t) { topology = t; }
    bool HasFlag(uint32_t f) const { return (flags & f) != 0; }
    void SetFlag(uint32_t f) { flags |= f; }
    void ClearFlag(uint32_t f) { flags &= ~f; }
    bool IsDepthTested() const { return HasFlag(Flag_DepthTest); }
    bool IsBlended() const { return HasFlag(Flag_BlendEnable); }
};

} // namespace bighero
