#include "render/QueueFamily.h"

namespace BigHero::Render
{
uint32_t SelectDedicatedTransferFamily(const std::vector<VkQueueFamilyProperties>& queues,
                                       uint32_t graphicsFamily) noexcept
{
    uint32_t computeTransferFallback = UINT32_MAX;
    for (uint32_t i = 0; i < static_cast<uint32_t>(queues.size()); ++i)
    {
        const VkQueueFlags flags = queues[i].queueFlags;
        const bool hasTransfer = (flags & VK_QUEUE_TRANSFER_BIT) != 0;
        const bool hasGraphics = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool hasCompute = (flags & VK_QUEUE_COMPUTE_BIT) != 0;
        if (!hasTransfer || hasGraphics || i == graphicsFamily)
            continue; // 必须是独立于图形的传输族
        if (!hasCompute)
            return i; // 纯传输 DMA 族：最佳，立即返回
        if (computeTransferFallback == UINT32_MAX)
            computeTransferFallback = i; // compute+transfer 独立族：次优，记下来
    }
    return computeTransferFallback;
}
} // namespace BigHero::Render
