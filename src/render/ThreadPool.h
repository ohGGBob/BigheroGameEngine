#pragma once
// 固定线程数的工作线程池：把一批相互独立的任务并行执行并等待全部完成。
// 用于渲染管线的多线程命令录制（如点光源立方体阴影 6 面并行录制到独立 command buffer）。
// 纯逻辑、无 Vulkan 依赖，可单测。
//
// 商业化增强：
//   - ParallelFor(count, fn(idx))：把 [0,count) 连续区间按工作线程数切块并行处理，
//     是并行渲染/粒子的常用入口，比手工收集任务更简洁。
//   - Run() 返回完成的任务数；Stats() 提供累计提交/完成数与峰值排队深度，
//     便于诊断负载均衡与调度开销。
//   - 保持既有 Run(tasks) 接口兼容。

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace BigHero::Render
{

class ThreadPool
{
  public:
    // 调度统计：累计提交/完成的任务数，以及队列峰值深度。
    struct Stats
    {
        std::uint64_t submitted = 0;
        std::uint64_t completed = 0;
        std::size_t peakQueueDepth = 0;
    };

    // 创建工作线程（threadCount==0 时退化为仅主线程顺序执行，便于调试/单测）
    explicit ThreadPool(uint32_t threadCount)
    {
        workers_.reserve(threadCount);
        for (uint32_t i = 0; i < threadCount; ++i)
            workers_.emplace_back([this] { workerLoop(); });
    }

    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_)
            if (w.joinable())
                w.join();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    [[nodiscard]] uint32_t ThreadCount() const noexcept { return static_cast<uint32_t>(workers_.size()); }

    // 并行执行 tasks（tasks[i] 传入工作线程索引）。等待全部完成后返回完成的任务数。
    // 无工作线程或任务为空时：空任务直接返回 0；单任务在当前线程执行。
    std::size_t Run(const std::vector<std::function<void(uint32_t)>>& tasks)
    {
        if (tasks.empty())
            return 0;
        if (workers_.empty())
        {
            for (size_t i = 0; i < tasks.size(); ++i)
            {
                tasks[i](static_cast<uint32_t>(i));
                ++stats_.completed;
            }
            return tasks.size();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t i = 0; i < tasks.size(); ++i)
            {
                queue_.push({tasks[i], static_cast<uint32_t>(i)});
                ++pending_;
                ++stats_.submitted;
                if (queue_.size() > stats_.peakQueueDepth)
                    stats_.peakQueueDepth = queue_.size();
            }
        }
        cv_.notify_all();

        // 主线程也参与消费，直到全部完成
        std::unique_lock<std::mutex> lock(mutex_);
        doneCv_.wait(lock, [this] { return pending_ == 0; });
        return tasks.size();
    }

    // 把 [0,count) 切块并行处理：fn(idx) 对每个索引执行一次。
    // 无工作线程时在当前线程顺序执行全部索引。
    // 返回实际调度处理的索引数（等于 count）。
    std::size_t ParallelFor(std::size_t count, const std::function<void(std::size_t)>& fn)
    {
        if (count == 0)
            return 0;

        const size_t nw = workers_.size();
        if (nw == 0)
        {
            for (size_t i = 0; i < count; ++i)
                fn(i);
            stats_.completed += count;
            return count;
        }

        std::vector<std::function<void(uint32_t)>> tasks;
        tasks.reserve(nw);

        // 切块：把 [0,count) 分成 nw 段，每段连续，由对应工作线程处理。
        const size_t chunk = (count + nw - 1) / nw;
        for (size_t w = 0; w < nw; ++w)
        {
            const size_t begin = w * chunk;
            if (begin >= count)
                break;
            const size_t end = std::min(begin + chunk, count);
            tasks.emplace_back(
                [fn, begin, end](uint32_t) {
                    for (std::size_t i = begin; i < end; ++i)
                        fn(i);
                });
        }
        return Run(tasks);
    }

    // 只读统计：累计提交/完成数与峰值排队深度。
    [[nodiscard]] Stats GetStats() const noexcept { return stats_; }
    // 清零统计，不影响未完成队列。
    void ResetStats() noexcept { stats_ = Stats{}; }

  private:
    struct Job
    {
        std::function<void(uint32_t)> fn;
        uint32_t index;
    };

    void workerLoop()
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty())
                    return;
                job = std::move(queue_.front());
                queue_.pop();
            }
            job.fn(job.index);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                --pending_;
                ++stats_.completed;
                if (pending_ == 0)
                    doneCv_.notify_all();
            }
        }
    }

    std::vector<std::thread> workers_;
    std::queue<Job> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable doneCv_;
    std::atomic<uint32_t> pending_{0};
    bool stop_ = false;
    Stats stats_;
};

} // namespace BigHero::Render
