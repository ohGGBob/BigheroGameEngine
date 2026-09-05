#include "render/ParallelCommandRecorder.h"
#include "core/VkCheck.h"
#include "render/Context.h"
#include "render/ThreadPool.h"

namespace BigHero::Render
{
ParallelCommandRecorder::~ParallelCommandRecorder()
{
    Destroy();
}

void ParallelCommandRecorder::Create(const Context& ctx, uint32_t workerCount, uint32_t frameCount)
{
    Destroy();
    ctx_ = &ctx;
    workerCount_ = workerCount;
    frameCount_ = frameCount;
    if (workerCount == 0 || frameCount == 0)
        return;

    pool_ = std::make_unique<ThreadPool>(workerCount);
    pools_.resize(workerCount, VK_NULL_HANDLE);
    buffers_.assign(frameCount, std::vector<VkCommandBuffer>(workerCount, VK_NULL_HANDLE));

    const VkDevice dev = ctx.Device();
    std::vector<VkCommandBuffer> tmp(frameCount);
    for (uint32_t w = 0; w < workerCount; ++w)
    {
        VkCommandPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        info.queueFamilyIndex = ctx.GraphicsFamily();
        VK_CHECK(vkCreateCommandPool(dev, &info, nullptr, &pools_[w]), "创建并行录制命令池");

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = pools_[w];
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = frameCount;
        VK_CHECK(vkAllocateCommandBuffers(dev, &alloc, tmp.data()), "分配并行录制命令缓冲");
        for (uint32_t f = 0; f < frameCount; ++f)
            buffers_[f][w] = tmp[f];
    }
}

void ParallelCommandRecorder::Destroy()
{
    if (ctx_ == nullptr)
        return;
    const VkDevice dev = ctx_->Device();
    for (VkCommandPool p : pools_)
        if (p != VK_NULL_HANDLE)
            vkDestroyCommandPool(dev, p, nullptr);
    pools_.clear();
    buffers_.clear();
    pool_.reset();
    ctx_ = nullptr;
    workerCount_ = 0;
    frameCount_ = 0;
}

void ParallelCommandRecorder::Reset(uint32_t frameIndex)
{
    if (ctx_ == nullptr || frameIndex >= frameCount_)
        return;
    std::vector<std::function<void(uint32_t)>> resets;
    resets.reserve(workerCount_);
    for (uint32_t w = 0; w < workerCount_; ++w)
    {
        VkCommandBuffer cb = buffers_[frameIndex][w];
        resets.emplace_back([cb](uint32_t) { VK_CHECK(vkResetCommandBuffer(cb, 0), "重置并行录制命令缓冲"); });
    }
    pool_->Run(resets);
}

void ParallelCommandRecorder::RecordParallel(const std::vector<std::function<void(VkCommandBuffer)>>& tasks,
                                             uint32_t frameIndex)
{
    if (ctx_ == nullptr || tasks.empty() || frameIndex >= frameCount_)
        return;
    if (tasks.size() > workerCount_)
        throw std::runtime_error("[ParallelCommandRecorder] 并行录制任务数超过工作线程数");
    std::vector<std::function<void(uint32_t)>> jobs;
    jobs.reserve(tasks.size());
    for (uint32_t t = 0; t < tasks.size(); ++t)
    {
        VkCommandBuffer cb = buffers_[frameIndex][t];
        const auto& fn = tasks[t];
        jobs.emplace_back([cb, &fn](uint32_t) { fn(cb); });
    }
    pool_->Run(jobs);
}
} // namespace BigHero::Render
