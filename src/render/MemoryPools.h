#pragma once
// 双池显存子分配门面：把 GpuAllocator（子分配策略）绑定到两类内存类型上，
// 让 Buffer/Image 不再各自 vkAllocateMemory（消除两套内存分配器并存的中间态）。
//
// 设计（.trae/documents/engine-architecture-refactor-plan.md 阶段2）：
//   - deviceLocal 池：256MB × maxBlocks 8（上限 2GB），首选纯显存（非 host visible）
//   - hostVisible 池：64MB × maxBlocks 8（上限 512MB），HOST_VISIBLE|HOST_COHERENT，
//     块级持久映射（建块时 vkMapMemory 一次，MappedOf 直接返回块内指针，免每次 map/unmap）
//   - minAlign = max(bufferImageGranularity, 256)：GpuBlockAllocator 的 minAlign 同时
//     取整分配大小与偏移，保证 buffer/image 可安全同块共存
//   - 回退：请求超过块大小、memoryTypeBits 不含池类型、或池类型不满足要求属性
//     → 返回 invalid，调用方走原独占 vkAllocateMemory 路径（大资源/特殊内存类型）
//
// 池编码：GpuAllocation.block 最高位标记 host 池（kHostPoolBit），低 31 位为块索引，
// 使 MemoryOf/MappedOf/Free 无需额外上下文即可定位池。
//
// 线程模型：单线程假设（分配仅发生在初始化/加载/重建期）。

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vulkan/vulkan.h>

#include "render/GpuAllocator.h"

namespace BigHero::Render
{
class MemoryPools
{
  public:
    MemoryPools(VkPhysicalDevice gpu, VkDevice device);
    ~MemoryPools();

    MemoryPools(const MemoryPools&) = delete;
    MemoryPools& operator=(const MemoryPools&) = delete;

    // 子分配：requiredProps 决定路由（含 HOST_VISIBLE → host 池，否则 device 池）。
    // memReq 来自 vkGetBufferMemoryRequirements / vkGetImageMemoryRequirements。
    // 失败（超出块大小/内存类型不匹配/池不可用）返回 invalid，调用方回退独占分配。
    GpuAllocation Alloc(VkMemoryPropertyFlags requiredProps, const VkMemoryRequirements& memReq);
    void Free(const GpuAllocation& a);

    // 该分配所属块的 VkDeviceMemory（vkBindBufferMemory 的 memory 参数）
    [[nodiscard]] VkDeviceMemory MemoryOf(const GpuAllocation& a) const;
    // host 池分配的持久映射指针（块首 + offset）；device 池分配返回 nullptr
    [[nodiscard]] void* MappedOf(const GpuAllocation& a) const;

    // 统计日志：各池块数/用量 + 独占分配回退次数
    void LogStats() const;

  private:
    static constexpr uint32_t kHostPoolBit = 0x80000000u;
    static constexpr uint32_t kMaxBlocks = 8;

    VkDeviceMemory AllocateRaw(uint32_t typeIndex, VkDeviceSize size);
    // 选择满足 required 且不含 avoid 的内存类型；两轮匹配（首轮避开 avoid，轮次退让）
    [[nodiscard]] uint32_t FindMemoryType(VkMemoryPropertyFlags required, VkMemoryPropertyFlags avoid) const;
    [[nodiscard]] bool TypeSatisfies(uint32_t typeIndex, VkMemoryPropertyFlags requiredProps) const;
    [[nodiscard]] static uint32_t DecodeBlock(const GpuAllocation& a) noexcept { return a.block & ~kHostPoolBit; }
    [[nodiscard]] static bool IsHostAlloc(const GpuAllocation& a) noexcept { return (a.block & kHostPoolBit) != 0; }

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memProps_{};
    uint32_t deviceTypeIdx_ = UINT32_MAX;
    uint32_t hostTypeIdx_ = UINT32_MAX;

    std::unique_ptr<GpuAllocator> devicePool_;
    std::unique_ptr<GpuAllocator> hostPool_;
    std::unordered_map<uint32_t, void*> hostMapped_; // host 池块持久映射（块索引 → 指针）
    uint32_t fallbacks_ = 0;                         // 回退独占分配的请求次数
};
} // namespace BigHero::Render
