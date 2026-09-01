#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
/// GPU 时间戳性能剖析器
/// 在命令缓冲的渲染阶段边界写入时间戳查询，提交后回读并按设备周期换算为毫秒，
/// 得到阴影预通道 / 场景通道 / UI 通道以及整帧的 GPU 耗时。
/// 设备不支持图形时间戳查询（Vulkan 1.2+ 核心特性）时退化为空操作，所有耗时恒为 0。
class GpuProfiler
{
  public:
    GpuProfiler() = default;

    void Init(VkDevice device, uint32_t maxFramesInFlight, float timestampPeriodNs)
    {
        device_ = device;
        maxFrames_ = maxFramesInFlight;
        period_ = (timestampPeriodNs > 0.0f) ? timestampPeriodNs : 1.0f;

        constexpr uint32_t queryCount = kPerFrame * 4; // 每帧 4 个时间戳（最坏情况）
        pool_.resize(maxFramesInFlight, VK_NULL_HANDLE);
        for (uint32_t f = 0; f < maxFramesInFlight; ++f)
        {
            VkQueryPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = queryCount;
            if (vkCreateQueryPool(device_, &info, nullptr, &pool_[f]) != VK_SUCCESS)
                throw std::runtime_error("GpuProfiler: 创建时间戳查询池失败");
        }
    }

    void Release() noexcept
    {
        if (device_ != VK_NULL_HANDLE)
        {
            for (VkQueryPool p : pool_)
                if (p != VK_NULL_HANDLE)
                    vkDestroyQueryPool(device_, p, nullptr);
        }
        pool_.clear();
        device_ = VK_NULL_HANDLE;
    }

    ~GpuProfiler() { Release(); }

    /// 在每个命令缓冲录制前重置本帧查询池，避免读到上一轮未完成的结果
    void Reset(VkCommandBuffer cmd, uint32_t frameIndex) const noexcept
    {
        if (!Active(frameIndex))
            return;
        vkCmdResetQueryPool(cmd, pool_[frameIndex], 0, kPerFrame);
    }

    /// 在当前命令缓冲写入一个时间戳（slot 0..3 对应 ShadowStart/SceneStart/UiStart/FrameEnd）
    void Write(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t slot) const noexcept
    {
        if (!Active(frameIndex) || slot >= kPerFrame)
            return;
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_[frameIndex], slot);
    }

    /// 在下一轮复用同一帧索引、其 GPU 工作已完成时回读并换算为毫秒
    void Resolve(uint32_t frameIndex) noexcept
    {
        if (!Active(frameIndex))
            return;
        std::vector<uint64_t> data(kPerFrame, 0);
        const VkResult res =
            vkGetQueryPoolResults(device_, pool_[frameIndex], 0, kPerFrame, data.size() * sizeof(uint64_t), data.data(),
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (res != VK_SUCCESS)
            return; // 尚未就绪（前几帧）则保留上次结果

        const uint64_t t0 = data[0]; // 阴影开始
        const uint64_t t1 = data[1]; // 场景开始（=阴影结束）
        const uint64_t t2 = data[2]; // UI 开始（=场景结束）
        const uint64_t t3 = data[3]; // 整帧结束（=UI 结束）
        frameMs_ = (t3 - t0) * period_ / 1.0e6f;
        shadowMs_ = (t1 - t0) * period_ / 1.0e6f;
        sceneMs_ = (t2 - t1) * period_ / 1.0e6f;
        uiMs_ = (t3 - t2) * period_ / 1.0e6f;
    }

    [[nodiscard]] bool IsSupported() const noexcept { return !pool_.empty() && device_ != VK_NULL_HANDLE; }
    [[nodiscard]] float FrameMs() const noexcept { return frameMs_; }
    [[nodiscard]] float ShadowMs() const noexcept { return shadowMs_; }
    [[nodiscard]] float SceneMs() const noexcept { return sceneMs_; }
    [[nodiscard]] float UiMs() const noexcept { return uiMs_; }

  private:
    static constexpr uint32_t kPerFrame = 4;

    [[nodiscard]] bool Active(uint32_t frameIndex) const noexcept
    {
        return device_ != VK_NULL_HANDLE && frameIndex < pool_.size() && pool_[frameIndex] != VK_NULL_HANDLE;
    }

    VkDevice device_ = VK_NULL_HANDLE;
    std::vector<VkQueryPool> pool_;
    uint32_t maxFrames_ = 0;
    float period_ = 1.0f; // 每 tick 的纳秒数
    float frameMs_ = 0.0f;
    float shadowMs_ = 0.0f;
    float sceneMs_ = 0.0f;
    float uiMs_ = 0.0f;
};
} // namespace BigHero::Render
