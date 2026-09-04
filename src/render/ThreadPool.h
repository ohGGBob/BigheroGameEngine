#pragma once
// 固定线程数的工作线程池：把一批相互独立的任务并行执行并等待全部完成。
// 用于渲染管线的多线程命令录制（如点光源立方体阴影 6 面并行录制到独立 command buffer）。
// 纯逻辑、无 Vulkan 依赖，可单测。
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

    // 并行执行 tasks（tasks[i] 传入工作线程索引）。等待全部完成后返回。
    // 无工作线程或任务为空时：空任务直接返回；单任务在当前线程执行。
    void Run(const std::vector<std::function<void(uint32_t)>>& tasks)
    {
        if (tasks.empty())
            return;
        if (workers_.empty())
        {
            for (size_t i = 0; i < tasks.size(); ++i)
                tasks[i](static_cast<uint32_t>(i));
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t i = 0; i < tasks.size(); ++i)
            {
                queue_.push({tasks[i], static_cast<uint32_t>(i)});
                ++pending_;
            }
        }
        cv_.notify_all();

        // 主线程也参与消费，直到全部完成
        std::unique_lock<std::mutex> lock(mutex_);
        doneCv_.wait(lock, [this] { return pending_ == 0; });
    }

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
};
} // namespace BigHero::Render

