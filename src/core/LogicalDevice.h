#pragma once
#include <cstdint>
#include <string>

namespace bighero {

// LogicalDevice: a handle to a logical GPU device created from a physical
// device, owning one or more queues. Self-contained, std-lib only.
class LogicalDevice {
public:
    LogicalDevice() = default;
    LogicalDevice(uint64_t handle, uint32_t physicalDevice = 0)
        : handle_(handle), physicalDevice_(physicalDevice) {}

    void SetHandle(uint64_t h) { handle_ = h; }
    uint64_t Handle() const { return handle_; }
    void SetPhysicalDevice(uint32_t p) { physicalDevice_ = p; }
    uint32_t PhysicalDevice() const { return physicalDevice_; }
    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }

    void SetQueueFamilyCount(uint32_t c) { queueFamilyCount_ = c; }
    uint32_t QueueFamilyCount() const { return queueFamilyCount_; }
    void SetEnabledQueueCount(uint32_t c) { enabledQueueCount_ = c; }
    uint32_t EnabledQueueCount() const { return enabledQueueCount_; }

    void EnableFeature(uint32_t featureBit) { enabledFeatures_ |= featureBit; }
    void DisableFeature(uint32_t featureBit) { enabledFeatures_ &= ~featureBit; }
    uint32_t EnabledFeatures() const { return enabledFeatures_; }
    bool HasFeature(uint32_t featureBit) const { return (enabledFeatures_ & featureBit) != 0; }

    bool IsValid() const { return handle_ != 0; }
    void SetLost(bool b) { lost_ = b; }
    bool IsLost() const { return lost_; }

private:
    uint64_t handle_ = 0;
    uint32_t physicalDevice_ = 0;
    std::string name_;
    uint32_t queueFamilyCount_ = 0;
    uint32_t enabledQueueCount_ = 0;
    uint32_t enabledFeatures_ = 0;
    bool lost_ = false;
};

} // namespace bighero
