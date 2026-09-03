#pragma once
// 并行命令录制器：为渲染管线提供"多个无依赖 pass 并行录制到独立 command buffer"的能力。
//
// 设计要点（对标商业引擎多线程录制）：
//   - 每个工作线程一个 VkCommandPool（Vulkan 明确要求：每个线程独立 pool 是线程安全的前提），
//     每帧复用一组 command buffer（按帧槽索引），避免每帧分配。
//   - RecordParallel 把相互独立的任务（如点光源立方体阴影 6 面）并行录制到各自的 command buffer，
//     之后由调用方以同一 vkQueueSubmit 的 buffer 数组顺序提交（同提交内顺序执行，无需额外信号量）。
//
// 典型用法（Renderer::DrawFrame）：
//   recorder.Reset(frameIndex);
//   recorder.RecordParallel({ [&](VkCommandBuffer c){ cubeShadow.RecordFace(c, 0, draw); }, ... }, frameIndex);
//   // 提交：pCommandBuffers = recorder.Buffers(frameIndex) + 主命令缓冲
#include "render/ThreadPool.h"
#include <functional>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero
{
class Context;
namespace Render
{
class ThreadPool;

class ParallelCommandRecorder
{
  public:
    ParallelCommandRecorder() = default;
    ~ParallelCommandRecorder();

    ParallelCommandRecorder(const ParallelCommandRecorder&) = delete;
    ParallelCommandRecorder& operator=(const ParallelCommandRecorder&) = delete;

    // workerCount：并行度（如 6 面阴影）；frameCount：并行帧槽数（与 Renderer 一致）
    void Create(const Context& ctx, uint32_t workerCount, uint32_t frameCount);
    void Destroy();

    // 重置本帧槽的全部并行缓冲（并行执行）
    void Reset(uint32_t frameIndex);

    // 并行录制 count 个任务（tasks[i] 在独立线程录制到本帧槽第 i 个缓冲）。
    // 每个任务内部需自含 vkBeginCommandBuffer/…/vkEndCommandBuffer 完整生命周期。
    void RecordParallel(const std::vector<std::function<void(VkCommandBuffer)>>& tasks, uint32_t frameIndex);

    // 本帧槽的全部并行缓冲（供主提交按序前置）
    [[nodiscard]] const std::vector<VkCommandBuffer>& Buffers(uint32_t frameIndex) const noexcept
    {
        return buffers_[frameIndex];
    }
    [[nodiscard]] uint32_t WorkerCount() const noexcept { return workerCount_; }
    [[nodiscard]] bool IsValid() const noexcept { return ctx_ != nullptr; }

  private:
    const Context* ctx_ = nullptr;
    uint32_t workerCount_ = 0;
    uint32_t frameCount_ = 0;
    std::unique_ptr<ThreadPool> pool_;
    std::vector<VkCommandPool> pools_;                  // 每工作线程一个
    std::vector<std::vector<VkCommandBuffer>> buffers_; // [frame][worker]
};
} // namespace BigHero::Render
} // namespace BigHero
