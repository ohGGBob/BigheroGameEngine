#pragma once
// 作业调度器（JobSystem）：C++20 线程池 + 任务分组执行（parallel_for）。
// 纯标准库、仅头文件。
//
// 商业化价值：渲染/物理/寻路/资源加载等高吞吐并行工作负载在引擎内的标准调度原语；
// 是"利用多核"的基础设施（类似 Unity Job System / UE 的 TaskGraph）。
//
// 提供：
//   - Execute：把 job 投递到线程池异步执行（返回 future）。
//   - ParallelFor：把 [0,count) 分成若干块并行执行（每块处理一个区间）。
//   - WaitAll：真正阻塞等待所有"已投递完成的任务"队列排空（帧尾同步/测试屏障）。
//   - PendingCount：当前未完成任务数（调试/查询用）。
//   - 线程数可用 hardware_concurrency 探测。
//
// WaitAll 语义（真实等待）：
//   - 用 pending_ 计数在 Execute 时自增、任务完成时自减；
//   - WaitAll 在条件变量上阻塞，直到 pending_ == 0 才返回；
//   - 完成后通过 cv_ 唤醒等待者。这样任何依赖"所有任务已执行完"的帧尾/测试同步都安全。

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace BigHero::Core
{
class JobSystem
{
  public:
    explicit JobSystem(unsigned threadCount = 0)
    {
        if (threadCount == 0)
            threadCount = std::max(1u, std::thread::hardware_concurrency());
        running_ = true;
        for (unsigned i = 0; i < threadCount; ++i)
            workers_.emplace_back([this] { WorkerLoop(); });
    }
    ~JobSystem() { Shutdown(); }
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    // 异步执行一个任务，返回其 future（无需结果的调用方可忽略）。
    std::future<void> Execute(std::function<void()> job)
    {
        auto task = std::make_shared<std::packaged_task<void()>>(std::move(job));
        std::future<void> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mx_);
            ++pending_;
            tasks_.emplace([task, this] {
                (*task)(); // 任务本体
                std::lock_guard<std::mutex> lock(mx_);
                --pending_;
                if (pending_ == 0)
                    cv_.notify_all(); // 唤醒 WaitAll（真正等待直至排空）
            });
        }
        cv_.notify_one();
        return fut;
    }

    // 把 [0,count) 分成 grained 大小的块并行执行。等待所有块完成。
    void ParallelFor(size_t count, size_t grained = 1, std::function<void(size_t, size_t)> fn = {})
    {
        if (count == 0)
            return;
        const size_t chunk = std::max<size_t>(1, grained);
        std::vector<std::future<void>> futs;
        futs.reserve((count + chunk - 1) / chunk);
        for (size_t start = 0; start < count; start += chunk)
        {
            const size_t end = std::min(start + chunk, count);
            futs.push_back(Execute([start, end, &fn] { fn(start, end); }));
        }
        for (auto& f : futs)
            f.wait();
    }

    // 阻塞直到所有"已投递任务"完成。这是真正的等待屏障：
    // 若还有任务在跑或待执行，这里会挂起当前线程直至 pending_==0。
    void WaitAll()
    {
        std::unique_lock<std::mutex> lock(mx_);
        cv_.wait(lock, [this] { return pending_ == 0; });
    }

    // 当前未完成任务数（含正在执行与排队中）。
    [[nodiscard]] size_t PendingCount() const
    {
        std::lock_guard<std::mutex> lock(mx_);
        return pending_;
    }

    void Shutdown()
    {
        std::function<void()> dummy;
        {
            std::lock_guard<std::mutex> lock(mx_);
            if (!running_)
                return;
            running_ = false;
        }
        cv_.notify_all();
        for (auto& w : workers_)
            if (w.joinable())
                w.join();
        workers_.clear();
    }

  private:
    void WorkerLoop()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mx_);
                cv_.wait(lock, [this] { return !running_ || !tasks_.empty(); });
                if (!running_ && tasks_.empty())
                    return;
                job = std::move(tasks_.front());
                tasks_.pop();
            }
            job();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mx_;
    mutable std::condition_variable cv_;
    std::atomic<bool> running_{ false };
    size_t pending_ = 0;
};
} // namespace BigHero::Core
