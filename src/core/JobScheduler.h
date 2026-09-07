#pragma once
#include <vector>
#include <cstddef>
#include <functional>

namespace bighero {

// Simple cooperative job scheduler: a queue of tasks run in submission order.
// (No threads; suitable for frame-based task batching.)
class JobScheduler {
public:
    using Job = std::function<void()>;

    void Submit(Job job) { pending_.push_back(std::move(job)); }

    // Run all queued jobs now, in order.
    std::size_t RunAll() {
        std::size_t n = 0;
        for (auto& j : pending_) { if (j) { j(); ++n; } }
        pending_.clear();
        return n;
    }

    // Run at most `maxJobs` jobs (spread across frames if desired).
    std::size_t RunUpTo(std::size_t maxJobs) {
        std::size_t n = 0;
        while (!pending_.empty() && n < maxJobs) {
            auto j = std::move(pending_.front());
            pending_.erase(pending_.begin());
            if (j) { j(); ++n; }
        }
        return n;
    }

    std::size_t Pending() const { return pending_.size(); }
    bool Empty() const { return pending_.empty(); }
    void Clear() { pending_.clear(); }

private:
    std::vector<Job> pending_;
};

} // namespace bighero
