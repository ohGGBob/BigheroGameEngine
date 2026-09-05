#include "render/Buffer.h"
#include "core/VkCheck.h"
#include "core/VkUtils.h"
#include "render/Context.h"

#include <cstring>

namespace BigHero
{
void Buffer::Create(const Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps)
{
    Destroy();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    device_ = ctx.Device();
    VK_CHECK(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer_), "创建Buffer");

    VkMemoryRequirements memReq{};
    vkGetBufferMemoryRequirements(device_, buffer_, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = FindMemoryType(ctx.PhysicalDevice(), memReq.memoryTypeBits, memProps);
    if (allocInfo.memoryTypeIndex == UINT32_MAX)
        throw std::runtime_error("Buffer: 未找到满足属性的内存类型");

    VK_CHECK(vkAllocateMemory(device_, &allocInfo, nullptr, &memory_), "分配Buffer显存");
    VK_CHECK(vkBindBufferMemory(device_, buffer_, memory_, 0), "绑定Buffer显存");

    size_ = size;
    memProps_ = memProps;
}

void Buffer::Destroy()
{
    if (device_ == VK_NULL_HANDLE)
        return;

    if (buffer_ != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
    size_ = 0;
}

void Buffer::UploadData(const Context& ctx, const void* data, VkDeviceSize size) const
{
    if (data == nullptr || size == 0 || size > size_)
        throw std::runtime_error("Buffer::UploadData: 数据为空或超出缓冲容量");

    if (memProps_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        // 主机可见内存：直接映射写入
        void* mapped = nullptr;
        VK_CHECK(vkMapMemory(device_, memory_, 0, size, 0, &mapped), "映射Buffer内存");
        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(device_, memory_);
        return;
    }

    // 设备本地内存：staging缓冲上传后命令拷贝
    Buffer staging;
    staging.Create(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.UploadData(ctx, data, size);

    ctx.SubmitOneTime(
        [&](VkCommandBuffer cmd)
        {
            VkBufferCopy region{};
            region.size = size;
            vkCmdCopyBuffer(cmd, staging.Get(), buffer_, 1, &region);
        });
}

void Buffer::MoveFrom(Buffer& other) noexcept
{
    device_ = other.device_;
    buffer_ = other.buffer_;
    memory_ = other.memory_;
    size_ = other.size_;
    memProps_ = other.memProps_;

    other.device_ = VK_NULL_HANDLE;
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.size_ = 0;
}
} // namespace BigHero
