#pragma once
// 瞬态内存池（transient memory pool）：在一段连续内存上做偏移子分配，供渲染图做
// 资源别名复用（生命周期不重叠的资源共享同一块显存）。
//
// 纯逻辑、无 Vulkan 依赖，可离线单测：
//   - Allocate(size, alignment)：first-fit 查找空闲块，按 alignment 对齐后切出区间，返回偏移。
//   - Free(offset)：归还区间，与相邻空闲块合并（避免碎片）。
//   - Reset()：整池回收，供帧/重建复用。
//
// 上层 TransientAllocator 只负责 VkDeviceMemory 的创建/销毁与 vkBindImageMemory 绑定，
// 分配算法完全由此类承担。

#include <algorithm>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h> // VkDeviceSize

namespace BigHero::Render
{
class TransientMemoryPool
{
  public:
    static constexpr VkDeviceSize kInvalidOffset = ~static_cast<VkDeviceSize>(0);

    explicit TransientMemoryPool(VkDeviceSize totalSize) : total_(totalSize)
    {
        freeList_.push_back({0, totalSize});
    }

    // 分配 size 字节（自动向上对齐到 alignment），返回池内偏移；空间不足返回 kInvalidOffset。
    VkDeviceSize Allocate(VkDeviceSize size, VkDeviceSize alignment)
    {
        if (size == 0 || alignment == 0)
            return kInvalidOffset;

        for (size_t i = 0; i < freeList_.size(); ++i)
        {
            const VkDeviceSize aligned = AlignUp(freeList_[i].offset, alignment);
            const VkDeviceSize need = size + (aligned - freeList_[i].offset);
            if (need > freeList_[i].size)
                continue; // 该空闲块放不下，继续找

            // 从空闲块切出 [aligned, aligned+size)
            const VkDeviceSize oldSize = freeList_[i].size;
            if (need == oldSize)
                freeList_.erase(freeList_.begin() + static_cast<std::ptrdiff_t>(i));
            else
            {
                freeList_[i].offset = aligned + size;
                freeList_[i].size = oldSize - need;
            }
            allocList_.push_back({aligned, size});
            used_ += size;
            return aligned;
        }
        return kInvalidOffset;
    }

    // 归还 offset 处的分配，与相邻空闲块合并
    void Free(VkDeviceSize offset)
    {
        const auto it = std::find_if(allocList_.begin(), allocList_.end(),
                                     [offset](const Block& b) { return b.offset == offset; });
        if (it == allocList_.end())
            return; // 非法 offset 忽略
        const Block freed = *it;
        allocList_.erase(it);
        used_ -= freed.size;

        // 插入空闲列表（保持按 offset 升序），随后合并相邻块
        auto pos = std::upper_bound(freeList_.begin(), freeList_.end(), freed.offset,
                                    [](VkDeviceSize off, const Block& b) { return off < b.offset; });
        freeList_.insert(pos, freed);
        mergeAdjacent();
    }

    void Reset()
    {
        freeList_.clear();
        freeList_.push_back({0, total_});
        allocList_.clear();
        used_ = 0;
    }

    [[nodiscard]] VkDeviceSize TotalSize() const noexcept { return total_; }
    [[nodiscard]] VkDeviceSize UsedBytes() const noexcept { return used_; }
    [[nodiscard]] size_t AllocCount() const noexcept { return allocList_.size(); }

  private:
    struct Block
    {
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
    };

    static VkDeviceSize AlignUp(VkDeviceSize v, VkDeviceSize align) noexcept
    {
        return (v + align - 1) / align * align;
    }

    void mergeAdjacent()
    {
        if (freeList_.size() < 2)
            return;
        std::vector<Block> merged;
        merged.reserve(freeList_.size());
        merged.push_back(freeList_[0]);
        for (size_t i = 1; i < freeList_.size(); ++i)
        {
            Block& last = merged.back();
            const Block& cur = freeList_[i];
            if (last.offset + last.size == cur.offset)
                last.size += cur.size; // 相邻 → 合并
            else
                merged.push_back(cur);
        }
        freeList_.swap(merged);
    }

    VkDeviceSize total_ = 0;
    VkDeviceSize used_ = 0;
    std::vector<Block> freeList_;
    std::vector<Block> allocList_;
};
} // namespace BigHero::Render
