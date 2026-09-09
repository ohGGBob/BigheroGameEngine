#pragma once
#include <cstdint>
#include <vector>

namespace bighero {

// QueueSubmitInfo: describes a batch of command buffers submitted to a queue,
// with wait/signal semaphores. Self-contained, std-lib only.
class QueueSubmitInfo {
public:
    QueueSubmitInfo() = default;

    void SetQueueIndex(uint32_t q) { queueIndex_ = q; }
    uint32_t QueueIndex() const { return queueIndex_; }

    void AddCommandBuffer(uint64_t cmdBuffer) { commandBuffers_.push_back(cmdBuffer); }
    void AddCommandBuffers(const uint64_t* cbs, uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) commandBuffers_.push_back(cbs[i]);
    }
    size_t CommandBufferCount() const { return commandBuffers_.size(); }
    uint64_t CommandBufferAt(size_t i) const { return commandBuffers_[i]; }

    void AddWaitSemaphore(uint64_t semaphore, uint16_t stageMask = 0xFFFFu) {
        SemaphoreInfo s; s.semaphore = semaphore; s.stageMask = stageMask;
        waitSemaphores_.push_back(s);
    }
    void AddSignalSemaphore(uint64_t semaphore) { signalSemaphores_.push_back(semaphore); }
    size_t WaitSemaphoreCount() const { return waitSemaphores_.size(); }
    size_t SignalSemaphoreCount() const { return signalSemaphores_.size(); }
    const std::vector<uint64_t>& SignalSemaphores() const { return signalSemaphores_; }

    struct SemaphoreInfo { uint64_t semaphore = 0; uint16_t stageMask = 0xFFFFu; };
    const SemaphoreInfo& WaitSemaphoreAt(size_t i) const { return waitSemaphores_[i]; }

    bool IsValid() const { return !commandBuffers_.empty(); }
    void Clear() {
        commandBuffers_.clear(); waitSemaphores_.clear(); signalSemaphores_.clear();
    }

private:
    uint32_t queueIndex_ = 0;
    std::vector<uint64_t> commandBuffers_;
    std::vector<SemaphoreInfo> waitSemaphores_;
    std::vector<uint64_t> signalSemaphores_;
};

} // namespace bighero
