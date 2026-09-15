#include "render/TransientAllocator.h"
#include "core/VkCheck.h"
#include "render/Context.h"

#include <algorithm>

namespace BigHero::Render
{
void TransientAllocator::Create(const Context& ctx, VkDeviceSize poolSize, uint32_t memoryTypeIndex)
{
    Destroy();
    if (poolSize == 0)
        return;
    ctx_ = &ctx;

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = poolSize;
    alloc.memoryTypeIndex = memoryTypeIndex;
    VK_CHECK(vkAllocateMemory(ctx.Device(), &alloc, nullptr, &memory_), "分配transient显存池");

    size_ = poolSize;
    pool_ = TransientMemoryPool(poolSize);
}

void TransientAllocator::Destroy()
{
    if (ctx_ == nullptr)
        return;
    if (memory_ != VK_NULL_HANDLE)
        vkFreeMemory(ctx_->Device(), memory_, nullptr);
    memory_ = VK_NULL_HANDLE;
    size_ = 0;
    pool_ = TransientMemoryPool(0);
    ctx_ = nullptr;
}

VkDeviceSize TransientAllocator::AllocateAndBind(VkImage image, const VkMemoryRequirements& req)
{
    return AllocateAndBindShared(&image, &req, 1);
}

VkDeviceSize TransientAllocator::AllocateAndBindShared(const VkImage* images, const VkMemoryRequirements* reqs,
                                                       uint32_t count)
{
    if (memory_ == VK_NULL_HANDLE || count == 0 || images == nullptr || reqs == nullptr)
        return TransientMemoryPool::kInvalidOffset;

    // 槽位大小/对齐取组内最大值，保证每个成员都满足自身内存需求
    VkDeviceSize size = 0;
    VkDeviceSize align = 1;
    for (uint32_t i = 0; i < count; ++i)
    {
        size = std::max(size, reqs[i].size);
        align = std::max(align, reqs[i].alignment);
    }

    const VkDeviceSize offset = pool_.Allocate(size, align);
    if (offset == TransientMemoryPool::kInvalidOffset)
        return TransientMemoryPool::kInvalidOffset;

    for (uint32_t i = 0; i < count; ++i)
        VK_CHECK(vkBindImageMemory(ctx_->Device(), images[i], memory_, offset), "绑定图像到transient共享槽位");
    return offset;
}

void TransientAllocator::Free(VkDeviceSize offset)
{
    if (memory_ == VK_NULL_HANDLE)
        return;
    pool_.Free(offset);
}

void TransientAllocator::Reset()
{
    if (memory_ == VK_NULL_HANDLE)
        return;
    pool_.Reset();
}
} // namespace BigHero::Render
