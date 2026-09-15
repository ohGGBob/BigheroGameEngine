#pragma once
// 帧内瞬态上传池（frame transient staging）：每个帧槽位（frames-in-flight）一块常驻
// host-visible arena 缓冲，帧内 bump 分配切片供"每帧都变"的数据（实例/粒子）中转，
// 帧栅栏等待后整帧回收（Reset）。
//
// 替代的旧路径：每帧为每条上传 创建 staging Buffer（池子分配+绑定）→ 一次性提交
// （SubmitOneTime 内含 vkQueueWaitIdle 全队列停顿）→ 销毁。新路径把拷贝并入帧命令缓冲，
// 分配退化为指针 bump，回收退化为游标归零。
//
// 时序（关键约束）：
//   1) 更新阶段：把待上传数据登记为 StagedUpload（data 指针须活到录制阶段）；
//   2) DrawFrame 等待本槽位栅栏后 Reset(slot)：GPU 已读完该槽位上一帧切片，覆写安全；
//   3) 录制阶段（首个 pass 前）：RecordUploads —— bump 分配切片 → memcpy → vkCmdCopyBuffer，
//      收尾插一条 TRANSFER→VERTEX_INPUT 内存屏障保证顶点输入读取可见；
//   4) 本帧提交的栅栏再次覆盖"所有切片消费完成"的回收点，如此循环。
//
// 与 TransientAllocator 的分工：TransientAllocator 面向渲染图瞬态图像的显存别名复用
// （device-local）；本池面向每帧主机→设备数据中转（host-visible），二者共同构成帧资源
// 生命周期的瞬态显存路径。

#include "render/Buffer.h"

#include <cstddef>
#include <cstring>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;
namespace Render
{
class FrameStaging
{
  public:
    // 一条待上传的中转数据：dst 为设备本地目的缓冲，data 为主机端数据（须活到录制阶段）
    struct StagedUpload
    {
        VkBuffer dst = VK_NULL_HANDLE;
        const void* data = nullptr;
        VkDeviceSize bytes = 0;
    };

    // 创建每槽位常驻 arena（host 池子分配 + 持久映射）
    void Create(const Context& ctx, uint32_t frameSlots, VkDeviceSize perSlotBytes);
    void Destroy();

    // 槽位整帧回收（须在该槽位帧栅栏等待之后调用）
    void Reset(uint32_t slot);

    // 录制期：逐条分配 arena 切片并记录拷贝命令，返回成功条数（空间不足的条目跳过并告警）。
    // 有成功条目时收尾插入 TRANSFER→VERTEX_INPUT 屏障，保证本帧后续 pass 的顶点输入读取可见。
    uint32_t RecordUploads(VkCommandBuffer cmd, uint32_t slot, const StagedUpload* uploads, size_t count);

    [[nodiscard]] bool IsValid() const noexcept { return !arenas_.empty() && arenas_[0].IsValid(); }
    [[nodiscard]] uint32_t SlotCount() const noexcept { return static_cast<uint32_t>(arenas_.size()); }

  private:
    static constexpr VkDeviceSize kAlign = 16; // 切片对齐（拷贝偏移规范要求 4 的倍数，取 16 稳妥）
    std::vector<Buffer> arenas_;               // 每槽位一块常驻 host-visible arena
    std::vector<VkDeviceSize> bump_;           // 每槽位 bump 分配游标（Reset 归零）
};
} // namespace Render
} // namespace BigHero
