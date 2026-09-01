#pragma once
#include "ubo_structs.h"
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
/// 模板化UBO缓冲封装
/// 自动创建CPU可写、设备可见的Uniform缓冲，RAII自动释放资源
template<typename T>
    requires std::is_trivially_copyable_v<T>
struct UboBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mappedPtr = nullptr;
    VkDevice device = VK_NULL_HANDLE;

    /// 构造：创建缓冲+分配主机连贯内存+自动映射
    UboBuffer(VkDevice dev, VkPhysicalDevice physicalDev, uint32_t queueFamilyIndex) : device(dev)
    {
        CreateBuffer(queueFamilyIndex);
        AllocateMemory(physicalDev);
        BindMemory();
        MapHostMemory();
    }

    // 禁止拷贝，资源不可共享
    UboBuffer(const UboBuffer&) = delete;
    UboBuffer& operator=(const UboBuffer&) = delete;

    // 移动语义，转移资源所有权
    UboBuffer(UboBuffer&& other) noexcept { Swap(other); }
    UboBuffer& operator=(UboBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            Swap(other);
        }
        return *this;
    }

    /// 析构自动释放所有Vulkan资源
    ~UboBuffer() { Release(); }

    /// 将UBO数据写入映射内存（主机连贯内存无需刷新）
    void Update(const T& data) noexcept
    {
        if (mappedPtr == nullptr)
            return;
        std::memcpy(mappedPtr, &data, GetUboByteSize<T>());
    }

    /// 手动释放全部缓冲与内存资源
    void Release() noexcept
    {
        if (device == VK_NULL_HANDLE)
            return;

        if (mappedPtr != nullptr)
        {
            vkUnmapMemory(device, memory);
            mappedPtr = nullptr;
        }
        if (buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
        device = VK_NULL_HANDLE;
    }

    /// 判断缓冲资源是否有效
    [[nodiscard]] bool IsValid() const noexcept
    {
        return buffer != VK_NULL_HANDLE && memory != VK_NULL_HANDLE && mappedPtr != nullptr;
    }

  private:
    /// 交换两个UboBuffer资源（移动语义辅助函数）
    void Swap(UboBuffer& other) noexcept
    {
        std::swap(device, other.device);
        std::swap(buffer, other.buffer);
        std::swap(memory, other.memory);
        std::swap(mappedPtr, other.mappedPtr);
    }

    /// 创建VkBuffer对象
    void CreateBuffer(uint32_t queueFamilyIndex)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = GetUboByteSize<T>();
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.queueFamilyIndexCount = 1;
        bufferInfo.pQueueFamilyIndices = &queueFamilyIndex;

        const VkResult res = vkCreateBuffer(device, &bufferInfo, nullptr, &buffer);
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("UboBuffer: vkCreateBuffer failed");
        }
    }

    /// 查找并分配主机可见+连贯内存
    void AllocateMemory(VkPhysicalDevice physicalDev)
    {
        VkMemoryRequirements memReqs{};
        vkGetBufferMemoryRequirements(device, buffer, &memReqs);

        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physicalDev, &memProps);

        uint32_t suitableMemType = UINT32_MAX;
        constexpr VkMemoryPropertyFlags requiredFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            const bool bitMatch = (memReqs.memoryTypeBits & (1U << i)) != 0;
            const bool propMatch = (memProps.memoryTypes[i].propertyFlags & requiredFlags) == requiredFlags;
            if (bitMatch && propMatch)
            {
                suitableMemType = i;
                break;
            }
        }

        if (suitableMemType == UINT32_MAX)
        {
            throw std::runtime_error("UboBuffer: No host coherent visible memory type found");
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReqs.size;
        allocInfo.memoryTypeIndex = suitableMemType;

        const VkResult res = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("UboBuffer: vkAllocateMemory failed");
        }
    }

    /// 绑定内存到缓冲
    void BindMemory()
    {
        const VkResult res = vkBindBufferMemory(device, buffer, memory, 0);
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("UboBuffer: vkBindBufferMemory failed");
        }
    }

    /// 映射CPU可访问指针
    void MapHostMemory()
    {
        const VkResult res = vkMapMemory(device, memory, 0, GetUboByteSize<T>(), 0, &mappedPtr);
        if (res != VK_SUCCESS)
        {
            throw std::runtime_error("UboBuffer: vkMapMemory failed");
        }
    }
};
} // namespace BigHero::Render