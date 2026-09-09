#pragma once
#include <cstdint>

namespace bighero {

// DrawIndirect: describes an indirect draw command (vertex count, instance
// count, first vertex, first instance). Self-contained, std-lib only.
class DrawIndirect {
public:
    DrawIndirect() = default;
    DrawIndirect(uint32_t vertexCount, uint32_t instanceCount = 1,
                 uint32_t firstVertex = 0, uint32_t firstInstance = 0)
        : vertexCount_(vertexCount), instanceCount_(instanceCount),
          firstVertex_(firstVertex), firstInstance_(firstInstance) {}

    void SetVertexCount(uint32_t c) { vertexCount_ = c; }
    uint32_t VertexCount() const { return vertexCount_; }
    void SetInstanceCount(uint32_t c) { instanceCount_ = c; }
    uint32_t InstanceCount() const { return instanceCount_; }
    void SetFirstVertex(uint32_t v) { firstVertex_ = v; }
    uint32_t FirstVertex() const { return firstVertex_; }
    void SetFirstInstance(uint32_t i) { firstInstance_ = i; }
    uint32_t FirstInstance() const { return firstInstance_; }

    bool IsValid() const { return vertexCount_ > 0 && instanceCount_ > 0; }
    uint64_t TotalVertices() const { return (uint64_t)vertexCount_ * instanceCount_; }
    static uint32_t SizeBytes() { return 16; }

private:
    uint32_t vertexCount_=0, instanceCount_=0, firstVertex_=0, firstInstance_=0;
};

} // namespace bighero
