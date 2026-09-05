#include "render/TransientAllocator.h"
#include "core/VkCheck.h"
#include "render/Context.h"

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
    if (memory_ == VK_NULL_HANDLE || image == VK_NULL_HANDLE)
        return TransientMemoryPool::kInvalidOffset;

    const VkDeviceSize offset = pool_.Allocate(req.size, req.alignment);
    if (offset == TransientMemoryPool::kInvalidOffset)
        return TransientMemoryPool::kInvalidOffset;

    VK_CHECK(vkBindImageMemory(ctx_->Device(), image, memory_, offset), "绑定图像到transient显存池");
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
