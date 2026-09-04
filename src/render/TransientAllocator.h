#pragma once
// 瞬态显存分配器（transient allocator）：把一块 VkDeviceMemory 池化子分配，
// 供渲染图对"生命周期不重叠"的临时图像做别名复用（共享显存，显著降低峰值内存）。
//
// 层次划分：
//   - TransientMemoryPool：纯逻辑偏移分配/释放/合并（无 Vulkan，可单测）。
//   - TransientAllocator：Vulkan 包装——创建/销毁池显存，把 VkImage 绑定到池内偏移
//     （vkBindImageMemory 带 offset），对齐由池按内存需求 alignment 保证。
//
// 用法（渲染图驱动）：
//   1) 用 RegisterImage(name, image, layout, sizeBytes) 登记待复用的临时图；
//   2) Build() 后取各资源生命周期（ResourceLifetime），把区间不重叠的资源交给同一 allocator；
//   3) AllocateAndBind 分配偏移并绑定，Free 归还供后续资源复用；帧末 Reset 回收。

#include "render/TransientMemoryPool.h"
#include <cstdint>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;
namespace Render
{
class TransientAllocator
{
  public:
    TransientAllocator() = default;
    ~TransientAllocator() { Destroy(); }

    TransientAllocator(const TransientAllocator&) = delete;
    TransientAllocator& operator=(const TransientAllocator&) = delete;

    // 创建池显存：poolSize 字节、memoryTypeIndex 由调用方对目标 image 的 memoryTypeBits 计算。
    // 池内容不保证初始化（transient 语义：使用前由对应 pass 写入）。
    void Create(const Context& ctx, VkDeviceSize poolSize, uint32_t memoryTypeIndex);
    void Destroy();

    // 分配并对齐后把 image 绑定到池内偏移；返回偏移，空间不足返回 kInvalidOffset。
    // req 来自 vkGetImageMemoryRequirements（image 须已创建、未绑定）。
    [[nodiscard]] VkDeviceSize AllocateAndBind(VkImage image, const VkMemoryRequirements& req);

    // 归还偏移处的分配（供后续生命周期不重叠的资源复用）
    void Free(VkDeviceSize offset);

    // 整池回收（帧/重建边界）
    void Reset();

    [[nodiscard]] bool IsValid() const noexcept { return memory_ != VK_NULL_HANDLE; }
    [[nodiscard]] VkDeviceSize PoolSize() const noexcept { return size_; }
    [[nodiscard]] VkDeviceSize UsedBytes() const noexcept { return pool_.UsedBytes(); }

  private:
    const Context* ctx_ = nullptr;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    TransientMemoryPool pool_{0};
};
} // namespace BigHero::Render
} // namespace BigHero

