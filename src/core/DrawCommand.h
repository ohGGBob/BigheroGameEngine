#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// DrawCommand: a command describing a single draw call (indexed or non-indexed,
// instanced or not) to be submitted to a render queue. Self-contained.
class DrawCommand {
public:
    DrawCommand() = default;
    DrawCommand(uint32_t vao, uint32_t indexCount, uint32_t firstIndex = 0,
                uint32_t instanceCount = 1, int32_t baseVertex = 0)
        : vao_(vao), indexCount_(indexCount), firstIndex_(firstIndex),
          instanceCount_(instanceCount), baseVertex_(baseVertex) {}

    enum class Type : uint32_t { Triangles = 0x0004, Lines = 0x0001, Points = 0x0000 };

    void SetVao(uint32_t v) { vao_ = v; }
    uint32_t Vao() const { return vao_; }
    void SetIndexCount(uint32_t c) { indexCount_ = c; }
    uint32_t IndexCount() const { return indexCount_; }
    void SetFirstIndex(uint32_t i) { firstIndex_ = i; }
    uint32_t FirstIndex() const { return firstIndex_; }
    void SetInstanceCount(uint32_t c) { instanceCount_ = c; }
    uint32_t InstanceCount() const { return instanceCount_; }
    void SetBaseVertex(int32_t v) { baseVertex_ = v; }
    int32_t BaseVertex() const { return baseVertex_; }
    void SetPrimitive(Type t) { primitive_ = t; }
    Type Primitive() const { return primitive_; }
    bool IsIndexed() const { return indexCount_ > 0; }
    bool IsInstanced() const { return instanceCount_ > 1; }

    uint64_t EstimatedWork() const {
        return (uint64_t)indexCount_ * (uint64_t)instanceCount_;
    }

private:
    uint32_t vao_ = 0;
    uint32_t indexCount_ = 0;
    uint32_t firstIndex_ = 0;
    uint32_t instanceCount_ = 1;
    int32_t baseVertex_ = 0;
    Type primitive_ = Type::Triangles;
};

} // namespace bighero
