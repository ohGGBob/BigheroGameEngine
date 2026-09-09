#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <queue>

namespace bighero {

// ThreadPool: a worker pool that runs arbitrary tasks and returns futures.
// Distinct from JobQueue: each Push returns a std::future<T> for the result.
// Self-contained, std-lib only.
class ThreadPool {
public:
    explicit ThreadPool(size_t workers = 0) {
        if (workers == 0) workers = std::thread::hardware_concurrency();
        if (workers == 0) workers = 2;
        Start(workers);
    }
    ~ThreadPool() { Shutdown(); }
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Enqueue a callable; returns a future for its result.
    template <class F>
    auto Push(F&& f) -> std::future<decltype(f())> {
        using R = decltype(f());
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> res = task->get_future();
        {
            std::lock_guard<std::mutex> lk(mutex_);
            tasks_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return res;
    }

    void WaitIdle() {
        std::unique_lock<std::mutex> lk(mutex_);
        idleCv_.wait(lk, [this] { return busy_ == 0; });
    }
    bool IsBusy() const { return busy_ != 0; }
    size_t WorkerCount() const { return workers_.size(); }

    void Shutdown() {
        stop_ = true;
        cv_.notify_all();
        for (auto& w : workers_) if (w.joinable()) w.join();
        workers_.clear();
        stop_ = false;
    }

private:
    void Start(size_t n) {
        for (size_t i = 0; i < n; ++i) {
            workers_.emplace_back([this] { WorkerLoop(); });
        }
    }
    void WorkerLoop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mutex_);
                cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
                ++busy_;
            }
            task();
            --busy_;
            idleCv_.notify_all();
        }
    }

    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idleCv_;
    std::atomic<bool> stop_{false};
    std::atomic<int> busy_{0};
};

} // namespace bighero
