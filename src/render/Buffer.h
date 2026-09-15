#pragma once
#include <cstdint>
#include <vulkan/vulkan.h>

#include "render/GpuAllocator.h"

namespace BigHero
{
class Context;
namespace Render
{
class MemoryPools;
}

// VkBuffer+显存RAII封装：支持主机可见内存直写，或staging上传到设备本地内存
// 显存来源（阶段2B收敛）：优先 MemoryPools 子分配（双池），池不可用时回退独占 vkAllocateMemory
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
    // host 池持久映射指针（非 host 路径为 nullptr；FrameStaging 帧内中转写入用）
    [[nodiscard]] void* Mapped() const noexcept { return persistent_; }
    [[nodiscard]] bool IsValid() const noexcept { return buffer_ != VK_NULL_HANDLE; }

  private:
    void MoveFrom(Buffer& other) noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE; // 独占分配（池回退路径）
    Render::GpuAllocation alloc_;            // 池子分配句柄（valid 时 memory_ 无效）
    Render::MemoryPools* pools_ = nullptr;   // 池后端（alloc_.valid 时非空）
    void* persistent_ = nullptr;             // host 池块内持久映射指针（可直写）
    VkDeviceSize size_ = 0;
    VkMemoryPropertyFlags memProps_ = 0;
};
} // namespace BigHero
