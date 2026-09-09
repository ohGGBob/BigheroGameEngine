#pragma once
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace bighero {

// Semaphore: a counting semaphore that blocks on acquire when count is zero.
// Supports a max count cap. Self-contained, std-lib only.
class Semaphore {
public:
    explicit Semaphore(int initial = 0, int maxCount = -1) : count_(initial), max_(maxCount) {}

    void Acquire() {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return count_ > 0; });
        --count_;
    }
    bool TryAcquire() {
        std::lock_guard<std::mutex> lk(mutex_);
        if (count_ <= 0) return false;
        --count_;
        return true;
    }
    // Wait up to timeout_ms; returns true if acquired.
    bool AcquireFor(int timeoutMs) {
        std::unique_lock<std::mutex> lk(mutex_);
        bool ok = cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                               [this] { return count_ > 0; });
        if (ok) --count_;
        return ok;
    }
    void Release(int n = 1) {
        if (n <= 0) return;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            count_ += n;
            if (max_ >= 0 && count_ > max_) count_ = max_;
        }
        cv_.notify_all();
    }
    int Count() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return count_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int count_ = 0;
    int max_ = -1;
};

} // namespace bighero
