#include "render/FrameStaging.h"

#include "core/Log.h"
#include "render/Context.h"

namespace BigHero::Render
{
void FrameStaging::Create(const Context& ctx, uint32_t frameSlots, VkDeviceSize perSlotBytes)
{
    Destroy();
    if (frameSlots == 0 || perSlotBytes == 0)
        return;
    arenas_.resize(frameSlots);
    bump_.assign(frameSlots, 0);
    for (Buffer& arena : arenas_)
        arena.Create(ctx, perSlotBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    LOG_INFO("帧瞬态上传池就绪: " << frameSlots << " 槽位 x " << (perSlotBytes / 1024) << " KiB");
}

void FrameStaging::Destroy()
{
    for (Buffer& arena : arenas_)
        arena.Destroy();
    arenas_.clear();
    bump_.clear();
}

void FrameStaging::Reset(uint32_t slot)
{
    if (slot < bump_.size())
        bump_[slot] = 0;
}

uint32_t FrameStaging::RecordUploads(VkCommandBuffer cmd, uint32_t slot, const StagedUpload* uploads, size_t count)
{
    if (cmd == VK_NULL_HANDLE || slot >= arenas_.size() || uploads == nullptr || count == 0)
        return 0;
    const Buffer& arena = arenas_[slot];
    if (!arena.IsValid())
        return 0;
    void* host = arena.Mapped();
    if (host == nullptr)
        return 0;
    VkDeviceSize& cursor = bump_[slot];

    uint32_t recorded = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const StagedUpload& up = uploads[i];
        if (up.dst == VK_NULL_HANDLE || up.data == nullptr || up.bytes == 0)
            continue;
        const VkDeviceSize aligned = (cursor + kAlign - 1) / kAlign * kAlign;
        if (aligned + up.bytes > arena.Size())
        {
            LOG_ERROR("帧瞬态上传池槽位 " << slot << " 空间不足（需 " << up.bytes << "B，容量 " << arena.Size()
                                          << "B），跳过该条上传");
            continue;
        }
        std::memcpy(static_cast<std::byte*>(host) + aligned, up.data, static_cast<size_t>(up.bytes));
        VkBufferCopy region{};
        region.srcOffset = aligned;
        region.dstOffset = 0;
        region.size = up.bytes;
        vkCmdCopyBuffer(cmd, arena.Get(), up.dst, 1, &region);
        cursor = aligned + up.bytes;
        ++recorded;
    }

    if (recorded > 0)
    {
        // 拷贝（TRANSFER 写）→ 顶点输入读 的可见性屏障（实例/粒子数据均为顶点输入消费）
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &barrier, 0,
                             nullptr, 0, nullptr);
    }
    return recorded;
}
} // namespace BigHero::Render
