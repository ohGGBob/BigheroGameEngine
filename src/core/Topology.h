#pragma once
#include <cstdint>

namespace bighero {

// Topology: a primitive-topology descriptor with vertex/instance step,
// used by the draw-call path to interpret vertex buffer layouts.
struct Topology {
    enum class Primitive { Points, Lines, LineStrip, Triangles, TriangleStrip, TriangleFan };

    Primitive primitive = Primitive::Triangles;
    // Draw as instanced (or not).
    bool instanced = false;
    // Number of vertices per primitive instance (for stripping).
    int verticesPerInstance = 0;
    // Restart-index support for strip topologies (0xFFFFFFFF = disabled).
    uint32_t restartIndex = 0xFFFFFFFFu;

    Topology() = default;
    explicit Topology(Primitive p) : primitive(p) {}

    // Minimum number of vertices required to emit one primitive.
    int MinVerticesPerPrimitive() const {
        switch (primitive) {
            case Primitive::Points:      return 1;
            case Primitive::Lines:       return 2;
            case Primitive::LineStrip:   return 2;
            case Primitive::TriangleStrip:return 3;
            case Primitive::TriangleFan: return 3;
            default:                     return 3;
        }
    }
    // Whether this topology is a strip (allows primitive restart).
    bool IsStrip() const {
        return primitive == Primitive::LineStrip ||
               primitive == Primitive::TriangleStrip ||
               primitive == Primitive::TriangleFan;
    }
};

} // namespace bighero
