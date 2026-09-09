#pragma once
#include <cstdint>

namespace bighero {

// DrawIndexedIndirectCommand: a single record for an indexed indirect draw
// call, used with multi-draw-indirect. Self-contained, std-lib only.
class DrawIndexedIndirectCommand {
public:
    DrawIndexedIndirectCommand() = default;
    DrawIndexedIndirectCommand(uint32_t indexCount, uint32_t instanceCount,
                               uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
        : indexCount_(indexCount), instanceCount_(instanceCount), firstIndex_(firstIndex),
          vertexOffset_(vertexOffset), firstInstance_(firstInstance) {}

    void SetIndexCount(uint32_t c) { indexCount_ = c; }
    uint32_t IndexCount() const { return indexCount_; }
    void SetInstanceCount(uint32_t c) { instanceCount_ = c; }
    uint32_t InstanceCount() const { return instanceCount_; }
    void SetFirstIndex(uint32_t i) { firstIndex_ = i; }
    uint32_t FirstIndex() const { return firstIndex_; }
    void SetVertexOffset(int32_t v) { vertexOffset_ = v; }
    int32_t VertexOffset() const { return vertexOffset_; }
    void SetFirstInstance(uint32_t i) { firstInstance_ = i; }
    uint32_t FirstInstance() const { return firstInstance_; }

    // Pack into a tightly packed 20-byte record (5 x uint32).
    void Pack(uint32_t out[5]) const {
        out[0] = indexCount_; out[1] = instanceCount_; out[2] = firstIndex_;
        out[3] = (uint32_t)vertexOffset_; out[4] = firstInstance_;
    }
    static void Unpack(const uint32_t in[5], DrawIndexedIndirectCommand& cmd) {
        cmd.indexCount_ = in[0]; cmd.instanceCount_ = in[1]; cmd.firstIndex_ = in[2];
        cmd.vertexOffset_ = (int32_t)in[3]; cmd.firstInstance_ = in[4];
    }

    uint64_t TotalVertices() const {
        return (uint64_t)indexCount_ * (uint64_t)instanceCount_;
    }

private:
    uint32_t indexCount_ = 0;
    uint32_t instanceCount_ = 1;
    uint32_t firstIndex_ = 0;
    int32_t vertexOffset_ = 0;
    uint32_t firstInstance_ = 0;
};

} // namespace bighero
