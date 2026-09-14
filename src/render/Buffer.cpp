#include "render/Buffer.h"
#include "core/VkCheck.h"
#include "core/VkUtils.h"
#include "render/Context.h"
#include "render/MemoryPools.h"

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

    // 池路径：MemoryPools 子分配（失败/不可用时回退独占分配）
    pools_ = ctx.Pools();
    if (pools_ != nullptr)
        alloc_ = pools_->Alloc(memProps, memReq);

    if (alloc_.valid)
    {
        VK_CHECK(vkBindBufferMemory(device_, buffer_, pools_->MemoryOf(alloc_), alloc_.offset), "绑定Buffer显存(池)");
        persistent_ = pools_->MappedOf(alloc_); // host 池：块内持久映射指针；device 池：nullptr
    }
    else
    {
        pools_ = nullptr;
        alloc_ = {};
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = FindMemoryType(ctx.PhysicalDevice(), memReq.memoryTypeBits, memProps);
        if (allocInfo.memoryTypeIndex == UINT32_MAX)
            throw std::runtime_error("Buffer: 未找到满足属性的内存类型");

        VK_CHECK(vkAllocateMemory(device_, &allocInfo, nullptr, &memory_), "分配Buffer显存");
        VK_CHECK(vkBindBufferMemory(device_, buffer_, memory_, 0), "绑定Buffer显存");
    }

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
    if (alloc_.valid && pools_ != nullptr)
    {
        pools_->Free(alloc_);
        alloc_ = {};
        persistent_ = nullptr;
        pools_ = nullptr;
    }
    else if (memory_ != VK_NULL_HANDLE)
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
        // 主机可见内存：优先使用 host 池持久映射直写（免 map/unmap）
        if (persistent_ != nullptr)
        {
            std::memcpy(persistent_, data, static_cast<size_t>(size));
            return;
        }
        // 独占分配：每次 map/unmap
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
    alloc_ = other.alloc_;
    pools_ = other.pools_;
    persistent_ = other.persistent_;
    size_ = other.size_;
    memProps_ = other.memProps_;

    other.device_ = VK_NULL_HANDLE;
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.alloc_ = {};
    other.pools_ = nullptr;
    other.persistent_ = nullptr;
    other.size_ = 0;
}
} // namespace BigHero
