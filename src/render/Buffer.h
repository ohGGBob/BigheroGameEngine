#pragma once
#include <cstdint>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;

// VkBuffer+显存RAII封装：支持主机可见内存直写，或staging上传到设备本地内存
class Buffer
{
  public:
    Buffer() = default;
    ~Buffer() { Destroy(); }

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept { MoveFrom(other); }
    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            MoveFrom(other);
        }
        return *this;
    }

    void Create(const Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps);
    void Destroy();

    // 写入数据：主机可见内存直接映射写入；设备本地内存走staging缓冲+一次性命令拷贝
    void UploadData(const Context& ctx, const void* data, VkDeviceSize size) const;

    [[nodiscard]] VkBuffer Get() const noexcept { return buffer_; }
    [[nodiscard]] VkDeviceSize Size() const noexcept { return size_; }
    [[nodiscard]] bool IsValid() const noexcept { return buffer_ != VK_NULL_HANDLE; }

  private:
    void MoveFrom(Buffer& other) noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    VkMemoryPropertyFlags memProps_ = 0;
};
} // namespace BigHero

