#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bighero {

// PhysicalDevice: describes a physical GPU device (vendor, id, limits, queue
// families). Self-contained, std-lib only.
class PhysicalDevice {
public:
    PhysicalDevice() = default;
    explicit PhysicalDevice(uint32_t index) : index_(index) {}

    void SetIndex(uint32_t i) { index_ = i; }
    uint32_t Index() const { return index_; }
    void SetVendorId(uint32_t v) { vendorId_ = v; }
    uint32_t VendorId() const { return vendorId_; }
    void SetDeviceId(uint32_t d) { deviceId_ = d; }
    uint32_t DeviceId() const { return deviceId_; }
    void SetName(std::string n) { name_ = std::move(n); }
    const std::string& Name() const { return name_; }

    void AddQueueFamily(uint32_t index, uint32_t count, uint32_t flags) {
        QueueFamily qf; qf.index = index; qf.count = count; qf.flags = flags;
        queueFamilies_.push_back(qf);
    }
    size_t QueueFamilyCount() const { return queueFamilies_.size(); }
    struct QueueFamily { uint32_t index; uint32_t count; uint32_t flags; };
    const QueueFamily& QueueFamilyAt(size_t i) const { return queueFamilies_[i]; }
    void ClearQueueFamilies() { queueFamilies_.clear(); }

    void SetMaxUniformBufferRange(uint32_t r) { maxUniformBufferRange_ = r; }
    uint32_t MaxUniformBufferRange() const { return maxUniformBufferRange_; }
    void SetLimits(uint32_t maxTextureDimension, uint32_t maxImageDimension) {
        maxTextureDimension_ = maxTextureDimension; maxImageDimension_ = maxImageDimension;
    }
    uint32_t MaxTextureDimension() const { return maxTextureDimension_; }
    uint32_t MaxImageDimension() const { return maxImageDimension_; }

    bool IsValid() const { return name_.empty() == false || queueFamilies_.empty() == false; }

private:
    uint32_t index_ = 0;
    uint32_t vendorId_ = 0, deviceId_ = 0;
    std::string name_;
    std::vector<QueueFamily> queueFamilies_;
    uint32_t maxUniformBufferRange_ = 16384;
    uint32_t maxTextureDimension_ = 4096, maxImageDimension_ = 4096;
};

} // namespace bighero
