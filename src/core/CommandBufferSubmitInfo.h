#pragma once
#include <cstdint>
#include <vector>
#include "QueueSubmitInfo_v2.h"

namespace bighero {

// CommandBufferSubmitInfo: describes a single command buffer to submit,
// including which wait/signal semaphores apply. Self-contained.
class CommandBufferSubmitInfo {
public:
    CommandBufferSubmitInfo() = default;
    explicit CommandBufferSubmitInfo(uint64_t commandBuffer) : commandBuffer_(commandBuffer) {}

    void SetCommandBuffer(uint64_t cb) { commandBuffer_ = cb; }
    uint64_t CommandBuffer() const { return commandBuffer_; }
    void SetStageFlags(uint16_t f) { stageFlags_ = f; }
    uint16_t StageFlags() const { return stageFlags_; }

    void SetWaitSemaphoreCount(uint32_t c) { waitCount_ = c; }
    uint32_t WaitSemaphoreCount() const { return waitCount_; }
    void SetSignalSemaphoreCount(uint32_t c) { signalCount_ = c; }
    uint32_t SignalSemaphoreCount() const { return signalCount_; }

    bool IsValid() const { return commandBuffer_ != 0; }
    static const char* StageName(uint16_t f) {
        switch (f) {
            case 0x1: return "Vertex";
            case 0x2: return "Fragment";
            case 0x4: return "Compute";
            default: return "Other";
        }
    }

private:
    uint64_t commandBuffer_ = 0;
    uint16_t stageFlags_ = 0;
    uint32_t waitCount_ = 0;
    uint32_t signalCount_ = 0;
};

} // namespace bighero
