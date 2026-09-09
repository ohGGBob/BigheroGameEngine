#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>

namespace bighero {

// JobQueue: a simple multi-worker task queue. Jobs are std::function<void()>.
// Start workers with Start(n), enqueue with Push, wait idle with WaitIdle.
// Self-contained, std-lib only.
class JobQueue {
public:
    JobQueue() = default;
    ~JobQueue() { Shutdown(); }

    // Spawn n worker threads.
    void Start(size_t workers = 2) {
        shutdown_ = false;
        for (size_t i = 0; i < workers; ++i) {
            workers_.emplace_back([this] { WorkerLoop(); });
        }
    }

    void Push(std::function<void()> job) {
        if (!job) return;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            jobs_.push_back(std::move(job));
        }
        ++pending_;
        cv_.notify_one();
    }

    // Block until all enqueued jobs are done.
    void WaitIdle() {
        std::unique_lock<std::mutex> lk(mutex_);
        idleCv_.wait(lk, [this] { return pending_ == 0; });
    }

    bool IsBusy() const { return pending_ != 0; }

    void Shutdown() {
        shutdown_ = true;
        cv_.notify_all();
        for (auto& w : workers_) if (w.joinable()) w.join();
        workers_.clear();
        shutdown_ = false;
    }

private:
    void WorkerLoop() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(mutex_);
                cv_.wait(lk, [this] { return shutdown_ || !jobs_.empty(); });
                if (shutdown_ && jobs_.empty()) return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            if (job) job();
            --pending_;
            idleCv_.notify_all();
        }
    }

    std::deque<std::function<void()>> jobs_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable idleCv_;
    std::atomic<bool> shutdown_{false};
    std::atomic<int> pending_{0};
};

} // namespace bighero
