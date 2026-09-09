#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// GpuSyncPoint: represents a GPU fence/sync marker on the CPU side. Tracks a
// monotonically increasing fence id, whether it has been signaled, and an
// optional label for debugging. The backend maps this onto a native fence.
class GpuSyncPoint {
public:
    GpuSyncPoint() {}
    explicit GpuSyncPoint(std::uint64_t fence) : fence_(fence), signaled_(false) {}

    void SetFence(std::uint64_t f) { fence_ = f; }
    std::uint64_t Fence() const { return fence_; }
    void SetFrame(std::uint64_t f) { frame_ = f; }
    std::uint64_t Frame() const { return frame_; }

    void Signal() { signaled_ = true; }
    void Reset() { signaled_ = false; }
    bool IsSignaled() const { return signaled_; }

    void SetLabel(const std::string& l) { label_ = l; }
    const std::string& Label() const { return label_; }

    // Typically truthy when a fence has been created but not yet signaled.
    bool IsPending() const { return fence_ != 0 && !signaled_; }
    bool IsValid() const { return fence_ != 0; }

private:
    std::uint64_t fence_ = 0;
    std::uint64_t frame_ = 0;
    bool signaled_ = false;
    std::string label_;
};

} // namespace bighero
