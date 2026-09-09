#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstddef>
#include <mutex>

namespace bighero {

// AsyncLoader: a lightweight async task queue. Tasks are jobs that may be
// completed immediately (synchronous simulated load) by calling RunOne until
// finished. Real async backend integration would pump the queue off-thread;
// here we allow safe progress polling. Pure stdlib.
class AsyncLoader {
public:
    using Task = std::function<bool()>; // returns true when done

    AsyncLoader() {}

    void Enqueue(Task t) {
        std::lock_guard<std::mutex> lk(mx_);
        pending_.push_back(std::move(t));
    }
    std::size_t Pending() const {
        std::lock_guard<std::mutex> lk(mx_);
        return pending_.size();
    }
    bool IsIdle() const { return Pending() == 0; }

    // Process the next task; returns true if a task was processed and finished.
    bool RunOne() {
        std::lock_guard<std::mutex> lk(mx_);
        if (pending_.empty()) return false;
        Task t = std::move(pending_.back());
        pending_.pop_back();
        // Run outside lock to avoid re-entrant deadlocks.
        lk.~lock_guard();
        bool done = t();
        return done;
    }
    void Clear() {
        std::lock_guard<std::mutex> lk(mx_);
        pending_.clear();
    }

private:
    std::vector<Task> pending_;
    mutable std::mutex mx_;
};

} // namespace bighero
