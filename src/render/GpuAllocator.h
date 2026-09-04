#pragma once
// GPU 显存分配器（自研轻量 VMA 替代）：在少量大块 VkDeviceMemory 之上做
// 子分配（sub-allocation），避免每次资源都调用昂贵的 vkAllocateMemory。
//
// 设计：
//   - GpuBlockAllocator（纯策略、可离线单测）：管理「单个大块」内的字节区间，
//     支持对齐分配、释放与相邻合并（free-list + 合并）。不依赖 Vulkan 运行。
//   - GpuAllocator（Vulkan 门面）：持有多块 GpuBlockAllocator 及其对应
//     VkDeviceMemory 句柄，按需扩容新建块；真正的 vkAllocateMemory 通过
//     注入的 CreateBlockFn 解耦，便于离线单测与不同后端（也便于将来换成 VMA）。
//
// 对齐约定：所有分配大小向上取整到块粒度 minAlign；分配偏移向上取整到请求
// 的 align（align >= minAlign）。释放后若相邻空闲段连续则自动合并，避免碎片化。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
// 一次子分配的句柄：第几块 + 块内偏移 + 大小。
struct GpuAllocation
{
    uint32_t block = 0;      // 第几块
    VkDeviceSize offset = 0; // 块内字节偏移（已对齐）
    VkDeviceSize size = 0;   // 分配大小（已向上取整到块粒度）
    bool valid = false;      // 是否有效（Free 或越界时为 false）
};

// 单块子分配器：free-list 实现，支持对齐分配、释放、相邻合并。
class GpuBlockAllocator
{
  public:
    GpuBlockAllocator(VkDeviceSize capacity, VkDeviceSize minAlign) : capacity_(capacity), minAlign_(minAlign)
    {
        free_.push_back({0, capacity_});
    }

    [[nodiscard]] VkDeviceSize Capacity() const noexcept { return capacity_; }
    [[nodiscard]] VkDeviceSize Used() const noexcept { return used_; }
    [[nodiscard]] VkDeviceSize FreeSpace() const noexcept { return capacity_ - used_; }
    [[nodiscard]] uint32_t AllocationCount() const noexcept { return allocCount_; }
    [[nodiscard]] bool Empty() const noexcept { return allocCount_ == 0; }

    // 分配：大小向上取整到 minAlign，偏移向上取整到 align；成功返回 valid 句柄。
    GpuAllocation Alloc(VkDeviceSize size, VkDeviceSize align)
    {
        GpuAllocation result{};
        if (size == 0)
            return result;
        const VkDeviceSize alignedSize = RoundUp(size, minAlign_);
        const VkDeviceSize a = std::max(align, minAlign_);
        for (size_t i = 0; i < free_.size(); ++i)
        {
            const FreeSpan seg = free_[i];
            const VkDeviceSize off = RoundUp(seg.off, a);
            if (off + alignedSize <= seg.off + seg.size)
            {
                // 重建空闲列表：移除当前段，必要时保留前导/剩余段
                std::vector<FreeSpan> next;
                next.reserve(free_.size() + 1);
                const VkDeviceSize segEnd = seg.off + seg.size;
                for (size_t j = 0; j < free_.size(); ++j)
                {
                    if (j == i)
                    {
                        if (off > free_[j].off)
                            next.push_back({free_[j].off, off - free_[j].off});
                        if (off + alignedSize < segEnd)
                            next.push_back({off + alignedSize, segEnd - (off + alignedSize)});
                    }
                    else
                    {
                        next.push_back(free_[j]);
                    }
                }
                free_ = std::move(next);
                result.block = 0;
                result.offset = off;
                result.size = alignedSize;
                result.valid = true;
                used_ += alignedSize;
                ++allocCount_;
                return result;
            }
        }
        return result; // 空间不足
    }

    // 释放：归还区间，随后合并相邻空闲段。
    void Free(const GpuAllocation& a)
    {
        if (!a.valid || a.size == 0)
            return;
        free_.push_back({a.offset, a.size});
        used_ -= a.size;
        --allocCount_;
        Coalesce();
    }

  private:
    struct FreeSpan
    {
        VkDeviceSize off = 0;
        VkDeviceSize size = 0;
    };

    static VkDeviceSize RoundUp(VkDeviceSize v, VkDeviceSize a)
    {
        if (a == 0)
            return v;
        return (v + a - 1) / a * a;
    }

    // 按偏移排序后合并相邻空闲段（prev.end == cur.off 则合并）
    void Coalesce()
    {
        std::sort(free_.begin(), free_.end(), [](const FreeSpan& a, const FreeSpan& b) { return a.off < b.off; });
        std::vector<FreeSpan> out;
        for (const FreeSpan& f : free_)
        {
            if (!out.empty() && out.back().off + out.back().size == f.off)
                out.back().size += f.size;
            else
                out.push_back(f);
        }
        free_ = std::move(out);
    }

    std::vector<FreeSpan> free_;
    VkDeviceSize capacity_ = 0;
    VkDeviceSize minAlign_ = 1;
    VkDeviceSize used_ = 0;
    uint32_t allocCount_ = 0;
};

// 块后端创建回调：返回该块对应的 VkDeviceMemory（Vulkan 侧实际分配）。
// 离线单测注入空实现（不真正分配显存）即可验证分配策略。
using CreateBlockFn = std::function<VkDeviceMemory(uint32_t blockIndex, VkDeviceSize size)>;

// 单块：子分配器 + 其实际显存句柄。
struct GpuBlock
{
    GpuBlockAllocator allocator;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

// 多块显存分配器（门面）。策略在 GpuBlockAllocator，Vulkan 绑定在 MemoryOf/外部 vkBind。
class GpuAllocator
{
  public:
    GpuAllocator(VkDeviceSize blockSize, VkDeviceSize minAlign, uint32_t maxBlocks, CreateBlockFn createBlock)
        : blockSize_(blockSize), minAlign_(minAlign), maxBlocks_(maxBlocks), createBlock_(std::move(createBlock))
    {
    }

    // 分配：优先在已有块中查找空间；都不够则新建块（受 maxBlocks 限制）。
    GpuAllocation Alloc(VkDeviceSize size, VkDeviceSize align)
    {
        for (uint32_t b = 0; b < static_cast<uint32_t>(blocks_.size()); ++b)
        {
            GpuAllocation a = blocks_[b].allocator.Alloc(size, align);
            if (a.valid)
            {
                a.block = b;
                return a;
            }
        }
        if (blocks_.size() >= maxBlocks_)
            return GpuAllocation{}; // 超出容量上限
        const uint32_t idx = static_cast<uint32_t>(blocks_.size());
        GpuBlock block{GpuBlockAllocator(blockSize_, minAlign_), VK_NULL_HANDLE};
        if (createBlock_)
            block.memory = createBlock_(idx, blockSize_);
        GpuAllocation a = block.allocator.Alloc(size, align);
        a.block = idx;
        blocks_.push_back(std::move(block));
        return a;
    }

    void Free(const GpuAllocation& a)
    {
        if (!a.valid || a.block >= blocks_.size())
            return;
        blocks_[a.block].allocator.Free(a);
    }

    [[nodiscard]] uint32_t BlockCount() const noexcept { return static_cast<uint32_t>(blocks_.size()); }
    [[nodiscard]] VkDeviceSize BlockSize() const noexcept { return blockSize_; }
    [[nodiscard]] uint32_t MaxBlocks() const noexcept { return maxBlocks_; }
    [[nodiscard]] VkDeviceSize Used(uint32_t block) const
    {
        return (block < blocks_.size()) ? blocks_[block].allocator.Used() : 0;
    }
    [[nodiscard]] bool BlockEmpty(uint32_t block) const
    {
        return (block < blocks_.size()) ? blocks_[block].allocator.Empty() : true;
    }

    // 取得某块的 VkDeviceMemory，供 vkBindBufferMemory(device, buf, mem, a.offset)。
    [[nodiscard]] VkDeviceMemory MemoryOf(uint32_t block) const
    {
        return (block < blocks_.size()) ? blocks_[block].memory : VK_NULL_HANDLE;
    }

  private:
    VkDeviceSize blockSize_;
    VkDeviceSize minAlign_;
    uint32_t maxBlocks_;
    CreateBlockFn createBlock_;
    std::vector<GpuBlock> blocks_;
};
} // namespace BigHero::Render

