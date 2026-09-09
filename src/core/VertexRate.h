#pragma once
#include <cstdint>

namespace bighero {

// VertexRate: how vertices/instances advance per draw/step.
// Self-contained, std-lib only.
class VertexRate {
public:
    enum class Rate : uint8_t { Vertex=0, Instance=1 };

    static bool IsValid(Rate r) { return r==Rate::Vertex || r==Rate::Instance; }
    static bool IsPerVertex(Rate r) { return r==Rate::Vertex; }
    static bool IsPerInstance(Rate r) { return r==Rate::Instance; }
    static const char* Name(Rate r) {
        switch (r) { case Rate::Vertex: return "Vertex"; case Rate::Instance: return "Instance"; }
        return "Unknown";
    }
};

} // namespace bighero
